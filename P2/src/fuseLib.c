#include "fuseLib.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/kdev_t.h>

/**
 * @brief Modifies the data size originally reserved by an inode, reserving or removing space if needed.
 *
 * @param idxNode inode number
 * @param newSize new size for the inode
 * @return int
 **/
int resizeNode(uint64_t idxNode, size_t newSize) {
	NodeStruct *node = myFileSystem.nodes[idxNode];
	char block[BLOCK_SIZE_BYTES];
	int i, diff = newSize - node->fileSize;

	if(!diff)
		return 0;

	memset(block, 0, sizeof(char)*BLOCK_SIZE_BYTES);

	/// File size increases
	if(diff > 0) {

		/// Delete the extra conent of the last block if it exists and is not full
		if(node->numBlocks && node->fileSize % BLOCK_SIZE_BYTES) {
			int currentBlock = node->blocks[node->numBlocks - 1];
			if((lseek(myFileSystem.fdVirtualDisk, currentBlock * BLOCK_SIZE_BYTES, SEEK_SET) == (off_t) - 1) ||
			        (read(myFileSystem.fdVirtualDisk, &block, BLOCK_SIZE_BYTES) == -1)) {
				perror("Failed lseek/read in resizeNode");
				return -EIO;
			}
			int offBlock = node->fileSize % BLOCK_SIZE_BYTES;
			int bytes2Write = (diff > (BLOCK_SIZE_BYTES - offBlock)) ? BLOCK_SIZE_BYTES - offBlock : diff;
			for(i = 0; i < bytes2Write; i++) {
				block[offBlock++] = 0;
			}

			if((lseek(myFileSystem.fdVirtualDisk, currentBlock * BLOCK_SIZE_BYTES, SEEK_SET) == (off_t) - 1) ||
			        (write(myFileSystem.fdVirtualDisk, &block, BLOCK_SIZE_BYTES) == -1)) {
				perror("Failed lseek/write in resizeNode");
				return -EIO;
			}
		}

		/// File size in blocks after the increment
		int newBlocks = (newSize + BLOCK_SIZE_BYTES - 1) / BLOCK_SIZE_BYTES - node->numBlocks;
		if(newBlocks) {
			memset(block, 0, sizeof(char)*BLOCK_SIZE_BYTES);

			// We check that there is enough space
			if(newBlocks > myFileSystem.superBlock.numOfFreeBlocks)
				return -ENOSPC;

			myFileSystem.superBlock.numOfFreeBlocks -= newBlocks;
			int currentBlock = node->numBlocks;
			node->numBlocks += newBlocks;

			for(i = 0; currentBlock != node->numBlocks; i++) {
				if(myFileSystem.bitMap[i] == 0) {
					myFileSystem.bitMap[i] = 1;
					node->blocks[currentBlock] = i;
					currentBlock++;
					// Clean disk (necessary for truncate)
					if((lseek(myFileSystem.fdVirtualDisk, i * BLOCK_SIZE_BYTES, SEEK_SET) == (off_t) - 1) ||
					        (write(myFileSystem.fdVirtualDisk, &block, BLOCK_SIZE_BYTES) == -1)) {
						perror("Failed lseek/write in resizeNode");
						return -EIO;
					}
				}
			}
		}
		node->fileSize += diff;

	}
	/// File decreases
	else {
		// File size in blocks after truncation
		int numBlocks = (newSize + BLOCK_SIZE_BYTES - 1) / BLOCK_SIZE_BYTES;
		myFileSystem.superBlock.numOfFreeBlocks += (node->numBlocks - numBlocks);

		for(i = node->numBlocks; i > numBlocks; i--) {
			int nBloque = node->blocks[i - 1];
			myFileSystem.bitMap[nBloque] = 0;
			// Clean disk (it is not really necessary)
			if((lseek(myFileSystem.fdVirtualDisk, nBloque * BLOCK_SIZE_BYTES, SEEK_SET) == (off_t) - 1) ||
			        (write(myFileSystem.fdVirtualDisk, &block, BLOCK_SIZE_BYTES) == -1)) {
				perror("Failed lseek/write in resizeNode");
				return -EIO;
			}
		}
		node->numBlocks = numBlocks;
		node->fileSize += diff;
	}
	node->modificationTime = time(NULL);

	sync();
	
	/// Update all the information in the backup file
	updateSuperBlock(&myFileSystem);
	updateBitmap(&myFileSystem);
	updateNode(&myFileSystem, idxNode, node);

	return 0;
}

/**
 * @brief It formats the access mode of a file so it is compatible on screen, when it is printed
 *
 * @param mode mode
 * @param str output with the access mode in string format
 * @return void
 **/
void mode_string(mode_t mode, char *str) {
	str[0] = mode & S_IRUSR ? 'r' : '-';
	str[1] = mode & S_IWUSR ? 'w' : '-';
	str[2] = mode & S_IXUSR ? 'x' : '-';
	str[3] = mode & S_IRGRP ? 'r' : '-';
	str[4] = mode & S_IWGRP ? 'w' : '-';
	str[5] = mode & S_IXGRP ? 'x' : '-';
	str[6] = mode & S_IROTH ? 'r' : '-';
	str[7] = mode & S_IWOTH ? 'w' : '-';
	str[8] = mode & S_IXOTH ? 'x' : '-';
	str[9] = '\0';
}

/**
 * @brief Obtains the file attributes of a file just with the filename
 * 
 * Help from FUSE:
 * 
 * The 'st_dev' and 'st_blksize' fields are ignored. The 'st_ino' field is ignored except if the 'use_ino' mount option is given.
 *
 *		struct stat {
 *			ino_t     st_ino;     // inode number
 *			mode_t    st_mode;    // protection
 *			nlink_t   st_nlink;   // number of hard links
 *			uid_t     st_uid;     // user ID of owner
 *			gid_t     st_gid;     // group ID of owner
 *			dev_t     st_rdev;    // device ID (if special file)
 *			off_t     st_size;    // total size, in bytes
 *			blkcnt_t  st_blocks;  // number of 512B blocks allocated
 *			time_t    st_atime;   // time of last access
 *			time_t    st_mtime;   // time of last modification (file's content were changed)
 *			time_t    st_ctime;   // time of last status change (the file's inode was last changed)
 *		};
 *
 * @param path full file path
 * @param stbuf file attributes
 * @return 0 on success and <0 on error
 **/
static int my_getattr(const char *path, struct stat *stbuf) {
	NodeStruct *node;
	int idxDir;

	fprintf(stderr, "--->>>my_getattr: path %s\n", path);

	memset(stbuf, 0, sizeof(struct stat));

	/// Directory attributes
	if(strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		stbuf->st_uid = getuid();
		stbuf->st_gid = getgid();
		stbuf->st_mtime = stbuf->st_ctime = myFileSystem.superBlock.creationTime;
		return 0;
	}

	/// Rest of the world
	if((idxDir = findFileByName(&myFileSystem, (char *)path + 1)) != -1) {
		node = myFileSystem.nodes[myFileSystem.directory.files[idxDir].nodeIdx];
		//Regular file...
		if (node->type == 0){
			stbuf->st_size = node->fileSize;
			stbuf->st_mode = S_IFREG;
			stbuf->st_nlink = 1;
			stbuf->st_uid = getuid();
			stbuf->st_gid = getgid();
			stbuf->st_mtime = stbuf->st_ctime = node->modificationTime;
		}
		//Directory...
		else if (node -> type == 1){

			stbuf->st_size = node->fileSize;
			stbuf->st_mode = S_IFDIR;
			stbuf->st_nlink = 2;
			stbuf->st_uid = getuid();
			stbuf->st_gid = getgid();
			stbuf->st_mtime = stbuf->st_ctime = node->modificationTime;
		}
		//Other...
		else
			return -EISDIR;

		return 0;
	}

	return -ENOENT;
}

/**
 * @brief Reads the content of the root directory
 *
 * Help from FUSE:
 * 
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and passes zero to the filler function's offset.
 *    The filler function will not return '1' (unless an error happens), so the whole directory is read in a single readdir operation.
 *
 * 2) The readdir implementation keeps track of the offsets of the directory entries.
 *    It uses the offset parameter and always passes non-zero offset to the filler function. When the buffer is full
 *    (or an error happens) the filler function will return '1'.
 *
 * Function to add an entry in a readdir() operation:
 *	typedef int(* fuse_fill_dir_t)(void *buf, const char *name, const struct stat *stbuf, off_t off)
 *
 *	*Parameters
 *		-buf: the buffer passed to the readdir() operation
 *		-name: the file name of the directory entry
 *		-stat: file attributes, can be NULL
 *		-off: offset of the next entry or zero
 *	*Returns 1 if buffer is full, zero otherwise
 * 
 * @param path path to the root folder
 * @param buf buffer where all the forder entries will be stored
 * @param filler FUSE funtion used to fill the buffer
 * @param offset offset used by filler to fill the buffer
 * @param fi FUSE structure associated to the directory
 * @return 0 on success and <0 on error
 **/
static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler,  off_t offset, struct fuse_file_info *fi) {
	int i;

	fprintf(stderr, "--->>>my_readdir: path %s, offset %jd\n", path, (intmax_t)offset);

	if(strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	for(i = 0; i < MAX_FILES_PER_DIRECTORY; i++) {
		if(!(myFileSystem.directory.files[i].freeFile)) {
			if(filler(buf, myFileSystem.directory.files[i].fileName, NULL, 0) == 1)
				return -ENOMEM;
		}
	}

	return 0;
}

/**
 * @brief File opening
 *
 * Help from FUSE:
 * 
 * No creation (O_CREAT, O_EXCL) and by default also no truncation (O_TRUNC) flags will be passed to open().
 * If an application specifies O_TRUNC, fuse first calls truncate() and then open().
 *
 * Unless the 'default_permissions' mount option is given (limits access to the mounting user and root),
 * open should check if the operation is permitted for the given flags.
 *
 * Optionally open may also return an arbitrary filehandle in the fuse_file_info structure, which will be passed to all file operations.
 *
 *	struct fuse_file_info{
 *		int				flags				Open flags. Available in open() and release()
 *		unsigned long 	fh_old				Old file handle, don't use
 *		int 			writepage			In case of a write operation indicates if this was caused by a writepage
 *		unsigned int 	direct_io: 1		Can be filled in by open, to use direct I/O on this file
 *		unsigned int 	keep_cache: 1		Can be filled in by open, to indicate, that cached file data need not be invalidated.
 *		unsigned int 	flush: 1			Indicates a flush operation.
 *		unsigned int 	nonseekable: 1		Can be filled in by open, to indicate that the file is not seekable.
 *		unsigned int 	padding: 27			Padding. Do not use
 *		uint64_t 		fh					File handle. May be filled in by filesystem in open(). Available in all other file operations
 *		uint64_t 		lock_owner			Lock owner id.
 *		uint32_t 		poll_events			Requested poll events.
 *	}
 * 
 * @param path path to the root folder
 * @param fi FUSE structure associated to the file opened
 * @return 0 on success and <0 on error
 **/
static int my_open(const char *path, struct fuse_file_info *fi) {
	int idxDir;

	fprintf(stderr, "--->>>my_open: path %s, flags %d, %"PRIu64"\n", path, fi->flags, fi->fh);

	//if(findFileByName(path, &idxNodoI)){
	if((idxDir = findFileByName(&myFileSystem, (char *)path + 1)) == -1) {
		return -ENOENT;
	}
	//If file is not a regular file ...
	if (myFileSystem.nodes[idxDir]->type != 0)
		return -EISDIR;

	// Save the inode number in file handler to be used in the following calls
	fi->fh = myFileSystem.directory.files[idxDir].nodeIdx;

	return 0;
}


/**
 * @brief Write data on an opened file
 *
 * Help from FUSE
 *
 * Write should return exactly the number of bytes requested except on error.
 * 
 * @param path file path
 * @param buf buffer where we have data to write
 * @param size quantity of bytes to write
 * @param offset offset over the writing
 * @param fi FUSE structure linked to the opened file
 * @return 0 on success and <0 on error
 **/
static int my_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
	char buffer[BLOCK_SIZE_BYTES];
	int bytes2Write = size, totalWrite = 0, idxDir;
	NodeStruct *node = myFileSystem.nodes[fi->fh];

	fprintf(stderr, "--->>>my_write: path %s, size %zu, offset %jd, fh %"PRIu64"\n", path, size, (intmax_t)offset, fi->fh);

	//if(findFileByName(path, &idxNodoI)){
	if((idxDir = findFileByName(&myFileSystem, (char *)path + 1)) == -1) {
		return -ENOENT;
	}
	//If file is not a regular file ...
	if (myFileSystem.nodes[idxDir]->type != 0)
		return -EISDIR;
	
	// Increase the file size if it is needed
	if(resizeNode(fi->fh, size + offset) < 0)
		return -EIO;

	// Write data
	while(bytes2Write) {
		int i;
		int currentBlock, offBloque;
		currentBlock = node->blocks[offset / BLOCK_SIZE_BYTES];
		offBloque = offset % BLOCK_SIZE_BYTES;

		if((lseek(myFileSystem.fdVirtualDisk, currentBlock * BLOCK_SIZE_BYTES, SEEK_SET) == (off_t) - 1) ||
		        (read(myFileSystem.fdVirtualDisk, &buffer, BLOCK_SIZE_BYTES) == -1)) {
			perror("Failed lseek/read in my_write");
			return -EIO;
		}

		for(i = offBloque; (i < BLOCK_SIZE_BYTES) && (totalWrite < size); i++) {
			buffer[i] = buf[totalWrite++];
		}

		if((lseek(myFileSystem.fdVirtualDisk, currentBlock * BLOCK_SIZE_BYTES, SEEK_SET) == (off_t) - 1) ||
		        (write(myFileSystem.fdVirtualDisk, &buffer, BLOCK_SIZE_BYTES) == -1)) {
			perror("Failed lseek/write in my_write");
			return -EIO;
		}

		// Discont the written stuff
		bytes2Write -= (i - offBloque);
		offset += i;
	}
	sync();
	
	node->modificationTime = time(NULL);
	updateSuperBlock(&myFileSystem);
	updateBitmap(&myFileSystem);
	updateNode(&myFileSystem, fi->fh, node);

	return size;
}

/**
 * @brief Close the file
 *
 * Help from FUSE:
 * 
 * Release is called when there are no more references to an open file: all file descriptors are
 * closed and all memory mappings are unmapped.
 *
 * For every open() call there will be exactly one release() call with the same flags and file descriptor.
 * It is possible to have a file opened more than once, in which case only the last release will mean,
 * that no more reads/writes will happen on the file. The return value of release is ignored.
 * 
 * @param path file path
 * @param fi FUSE structure linked to the opened file
 * @return 0
 **/
static int my_release(const char *path, struct fuse_file_info *fi) {
	(void) path;
	(void) fi;
	int idxDir=findFileByName(&myFileSystem, (char *)path + 1);
	//comprobamos que el fichero no exista
	if(idxDir == -1)
		return -EEXIST;
	//If file is not a regular file ...
	if (myFileSystem.nodes[idxDir]->type != 0)
		return -EISDIR;

	fprintf(stderr, "--->>>my_release: path %s\n", path);

	return 0;
}

/**
 * @brief Create a file
 *
 * Help from FUSE:
 * 
 * This is called for creation of all non-directory, non-symlink nodes.
 * If the filesystem defines a create() method, then for regular files that will be called instead.
 * 
 * @param path file path
 * @param mode creation mode
 * @param device device where the device will be created (contains both major and minor numbers)
 * @return 0 on success and <0 on error
 **/
static int my_mknod(const char *path, mode_t mode, dev_t device) {
	char modebuf[10];

	mode_string(mode, modebuf);
	fprintf(stderr, "--->>>my_mknod: path %s, mode %s, major %d, minor %d\n", path, modebuf, (int)MAJOR(device), (int)MINOR(device));

	// We check that the length of the file name is correct
	if(strlen(path + 1) > myFileSystem.superBlock.maxLenFileName) {
		return -ENAMETOOLONG;
	}

	// There exist an available inode
	if(myFileSystem.numFreeNodes <= 0) {
		return -ENOSPC;
	}

	// There is still space for a new file
	if(myFileSystem.directory.numFiles >= MAX_FILES_PER_DIRECTORY) {
		return -ENOSPC;
	}
	// The directory exists
	if(findFileByName(&myFileSystem, (char *)path + 1) != -1)
		return -EEXIST;

	/// Update all the information in the backup file:
	int idxNodoI, idxDir;
	if((idxNodoI = findFreeNode(&myFileSystem)) == -1 || (idxDir = findFreeFile(&myFileSystem)) == -1) {
		return -ENOSPC;
	}

	// Update root folder
	myFileSystem.directory.files[idxDir].freeFile = false;
	myFileSystem.directory.numFiles++;
	strcpy(myFileSystem.directory.files[idxDir].fileName, path + 1);
	myFileSystem.directory.files[idxDir].nodeIdx = idxNodoI;
	myFileSystem.numFreeNodes--;

	// Fill the fields of the new inode
	if(myFileSystem.nodes[idxNodoI] == NULL)
		myFileSystem.nodes[idxNodoI] = malloc(sizeof(NodeStruct));

	myFileSystem.nodes[idxNodoI]->fileSize = 0;
	myFileSystem.nodes[idxNodoI]->numBlocks = 0;
	myFileSystem.nodes[idxNodoI]->type = 0;
	myFileSystem.nodes[idxNodoI]->modificationTime = time(NULL);
	myFileSystem.nodes[idxNodoI]->freeNode = false;

	reserveBlocksForNodes(&myFileSystem, myFileSystem.nodes[idxNodoI]->blocks, 0);

	updateDirectory(&myFileSystem);
	updateNode(&myFileSystem, idxNodoI, myFileSystem.nodes[idxNodoI]);

	return 0;
}

/**
 * @brief Change the size of a file
 *
 * @param path file path
 * @param size new size
 * @return 0 on success and <0 on error
 **/
static int my_truncate(const char *path, off_t size) {
	int idxDir;

	fprintf(stderr, "--->>>my_truncate: path %s, size %jd\n", path, size);

	idxDir=findFileByName(&myFileSystem, (char *)path + 1);
	//comprobamos que el fichero no exista
	if(idxDir == -1)
		return -EEXIST;
	//If file is not a regular file ...
	if (myFileSystem.nodes[idxDir]->type != 0)
		return -EISDIR;

	if((idxDir = findFileByName(&myFileSystem, (char *)path + 1)) == -1) {
		return -ENOENT;
	}

	// Modify the size
	if(resizeNode(myFileSystem.directory.files[idxDir].nodeIdx, size) < 0)
		return -EIO;

	return 0;
}

static int my_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *f){

	char buffer[BLOCK_SIZE_BYTES];
	int bytes2Read=size;
	NodeStruct *node = myFileSystem.nodes[f->fh];
	int bytesenfichero= node->fileSize;
	int i,pos=0;
	int idxDir=findFileByName(&myFileSystem, (char *)path + 1);
	//comprobamos que el fichero no exista
	if(idxDir == -1)
		return -EEXIST;
	//If file is not a regular file ...
	if (myFileSystem.nodes[idxDir]->type != 0)
		return -EISDIR;

	fprintf(stderr, "--->>>my_read: path %s, size %zu, offset %jd, fh %"PRIu64"\n", path, size, (intmax_t)offset, f->fh);
	
	//leemos
	while((bytes2Read >0) && (bytesenfichero >0)){
	
		int offbloque, bloqueactual;
		bloqueactual =	node->blocks[offset / BLOCK_SIZE_BYTES];
		offbloque = offset % BLOCK_SIZE_BYTES;
		int leido = 0;
		
		if((lseek(myFileSystem.fdVirtualDisk, (bloqueactual * BLOCK_SIZE_BYTES)+offbloque, SEEK_SET)) == (off_t) - 1){
			perror("Failed lseek/read in my_read");
			return -EIO;
		
		}

		leido= read(myFileSystem.fdVirtualDisk,&buffer,(BLOCK_SIZE_BYTES-offbloque));
		if(leido==-1){
			perror("Failed lseek/readblock in my_read");
			return -EIO;
		}

		for(i=0;i<leido;i++){
			buf[pos]=buffer[i];
			pos++;
		}

		bytes2Read -=leido;
		bytesenfichero -=leido;
		offset+=leido;
		
		
	}//fin while
	
	node->modificationTime = time(NULL);
	
	//guardamos en disco el contenido de la estructura modificada(superbloque,mapadebits,inodo)
	sync();
	
	if((bytesenfichero<=0) && (bytes2Read>0)){
		for(i=(size-bytes2Read);i<size;i++) buf[i]='\0';
		return (size-bytes2Read);	
	}	
	
	return size;
}

static int my_unlink(const char *path){
	
	//comprobamos que la longitud del nombre del archivo es correcta
	if(strlen(path + 1) > myFileSystem.superBlock.maxLenFileName) {
		return -ENAMETOOLONG;
	}
	
	int idxDir=findFileByName(&myFileSystem, (char *)path + 1);
	//comprobamos que el fichero no exista
	if(idxDir == -1)
		return -EEXIST;
	
	//If file is not a regular file ...
	if (myFileSystem.nodes[idxDir]->type != 0)
		return -EISDIR;

	//actualizo toda la informacion
	int idxNodoI= myFileSystem.directory.files[idxDir].nodeIdx;
	//trunco a cero el fichero y escribe en mapade bits es como my_truncate
	
	if(resizeNode(idxNodoI,0)<0)
		return -EIO;
	
	//actualizo el inodo
	myFileSystem.nodes[idxNodoI]->freeNode = true;
	updateNode(&myFileSystem, idxNodoI, myFileSystem.nodes[idxNodoI]);
	
	//libero el nodo
	free(myFileSystem.nodes[idxNodoI]);
	myFileSystem.nodes[idxNodoI]=NULL;
	
	//actualizo el dir raiz
	myFileSystem.directory.files[idxDir].freeFile = true;
	myFileSystem.directory.numFiles--;

	//guardo en disco el contenido de la estructura modificada que no se han tocado aun
	updateDirectory(&myFileSystem);
	myFileSystem.numFreeNodes++;

	sync();
	
	printf("mi funcion unlink \n");
	return 0;
	
}

int my_mkdir (const char *path, mode_t mode){

	char modebuf[10];

	mode_string(mode, modebuf);
	fprintf(stderr, "--->>>my_mkdir path %s, mode %s\n", path, modebuf);

	// We check that the length of the file name is correct
	if(strlen(path + 1) > myFileSystem.superBlock.maxLenFileName) {
		return -ENAMETOOLONG;
	}

	// There exist an available inode
	if(myFileSystem.numFreeNodes <= 0) {
		return -ENOSPC;
	}

	// There is still space for a new file
	if(myFileSystem.directory.numFiles >= MAX_FILES_PER_DIRECTORY) {
		return -ENOSPC;
	}
	// The directory exists
	if(findFileByName(&myFileSystem, (char *)path + 1) != -1)
		return -EEXIST;

	/// Update all the information in the backup file:
	int idxNodoI, idxDir;
	if((idxNodoI = findFreeNode(&myFileSystem)) == -1 || (idxDir = findFreeFile(&myFileSystem)) == -1) {
		return -ENOSPC;
	}

	// Update root folder
	myFileSystem.directory.files[idxDir].freeFile = false;
	myFileSystem.directory.numFiles++;
	strcpy(myFileSystem.directory.files[idxDir].fileName, path + 1);
	myFileSystem.directory.files[idxDir].nodeIdx = idxNodoI;
	myFileSystem.numFreeNodes--;

	// Fill the fields of the new inode
	if(myFileSystem.nodes[idxNodoI] == NULL)
		myFileSystem.nodes[idxNodoI] = malloc(sizeof(NodeStruct));

	myFileSystem.nodes[idxNodoI]->fileSize = 0;
	myFileSystem.nodes[idxNodoI]->numBlocks = 0;
	//Its a directory...s
	myFileSystem.nodes[idxNodoI]->type = 1;
	myFileSystem.nodes[idxNodoI]->modificationTime = time(NULL);
	myFileSystem.nodes[idxNodoI]->freeNode = false;

	reserveBlocksForNodes(&myFileSystem, myFileSystem.nodes[idxNodoI]->blocks, 0);

	updateDirectory(&myFileSystem);
	updateNode(&myFileSystem, idxNodoI, myFileSystem.nodes[idxNodoI]);

	return 0;
}

struct fuse_operations myFS_operations = {
	.getattr	= my_getattr,					// Obtain attributes from a file
	.readdir	= my_readdir,					// Read directory entries
	.truncate	= my_truncate,					// Modify the size of a file
	.open		= my_open,						// Oeen a file
	.write		= my_write,						// Write data into a file already opened
	.release	= my_release,					// Close an opened file
	.mknod		= my_mknod,						// Create a new file
	.read 		= my_read,						// Read data for an open file
	.unlink		= my_unlink,					// Delete a file
	.mkdir		= my_mkdir,						// Create an directory
};


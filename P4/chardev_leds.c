#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>	/* for copy_to_user */
#include <linux/cdev.h>
#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/version.h> /* For LINUX_VERSION_CODE */



MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modleds");
MODULE_AUTHOR("Semaforo Rojo");

/*
 *  Prototypes
 */
int init_module(void);
void cleanup_module(void);
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

#define SUCCESS 0
#define DEVICE_NAME "leds"	/* Dev name as it appears in /proc/devices   */
#define BUF_LEN 80		/* Max length of the message from the device */
#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0
/*
 * Global variables are declared as static, so are global within the file.
 */

dev_t start;
struct cdev* chardev_leds=NULL;
static int Device_Open = 0;	/* Is device open?
				 * Used to prevent multiple access to device */
static char msg[BUF_LEN];	/* The msg the device will give when asked */
static char *msg_Ptr;		/* This will be initialized every time the
				   device is opened successfully */
static int counter=0;		/* Tracks the number of times the character
				 * device has been opened */
static char kbuff[1];


static struct file_operations fops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release
};



struct tty_driver* kbd_driver= NULL;


/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void)	//llamada a teclado para encender leds
{
    printk(KERN_INFO "modleds: loading\n");	//pinta en el log
    printk(KERN_INFO "modleds: fgconsole is %x\n", fg_console);
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32) )
    return vc_cons[fg_console].d->port.tty->driver;
#else
    return vc_cons[fg_console].d->vc_tty->driver;
#endif
}

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask)	//Fija los leds
{
#if ( LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32) )
    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
#else
    return (handler->ops->ioctl) (vc_cons[fg_console].d->vc_tty, NULL, KDSETLED, mask);
#endif
}


int chardev_leds_init(void)	
{
	
    kbd_driver= get_kbd_driver_handler();
   // set_leds(kbd_driver,ALL_LEDS_ON);

    int major;		/* Major number assigned to our device driver */
    int minor;		/* Minor number assigned to the associated character device */

    /* Get available (major,minor) range */
    if (alloc_chrdev_region (&start, 0, 1,DEVICE_NAME)) {
        printk(KERN_INFO "Can't allocate chrdev_region()");
        return -ENOMEM;
    }

    /* Create associated cdev */
    if ((chardev_leds=cdev_alloc())==NULL) {
        printk(KERN_INFO "cdev_alloc() failed ");
        return -ENOMEM;
    }

    cdev_init(chardev_leds,&fops);

    if (cdev_add(chardev_leds,start,1)) {
        printk(KERN_INFO "cdev_add() failed ");
        return -ENOMEM;
    }

    major=MAJOR(start);
    minor=MINOR(start);

    printk(KERN_INFO "I was assigned major number %d. To talk to\n", major);
    printk(KERN_INFO "the driver, create a dev file with\n");
    printk(KERN_INFO "'sudo mknod -m 666 /dev/%s c %d %d'.\n", DEVICE_NAME, major,minor);
    printk(KERN_INFO "Try to cat and echo to the device file.\n");
    printk(KERN_INFO "Remove the device file and module when done.\n");

    return SUCCESS;
}

void chardev_leds_clean(void)
{
    set_leds(kbd_driver,ALL_LEDS_OFF);
    /* Destroy chardev */
    if (chardev_leds)
        cdev_del(chardev_leds);

    /*
     * Unregister the device
     */
    unregister_chrdev_region(start, 1);
}


/*
 * Called when a process tries to open the device file, like
 * "cat /dev/chardev"
 */
static int device_open(struct inode *inode, struct file *file)
{
    if (Device_Open)
        return -EBUSY;

    Device_Open++;

    /* Initialize msg */
    sprintf(msg, "I already told you %d times Hello world!\n", counter++);

    /* Initially, this points to the beginning of the message */
    msg_Ptr = msg;

    /* Increase the module's reference counter */
    try_module_get(THIS_MODULE);

    return SUCCESS;
}

/*
 * Called when a process closes the device file.
 */
static int device_release(struct inode *inode, struct file *file)
{
    Device_Open--;		/* We're now ready for our next caller */

    /*
     * Decrement the usage count, or else once you opened the file, you'll
     * never get get rid of the module.
     */
    module_put(THIS_MODULE);

    return 0;
}

static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t * offset)
{
    /*
     * Number of bytes actually written to the buffer
     */
    int bytes_to_read = length;

    /*
     * If we're at the end of the message,
     * return 0 -> end of file
     */
    if (*msg_Ptr == 0)
        return 0;

    /* Make sure we don't read more chars than
     * those remaining to read
    	 */
    if (bytes_to_read > strlen(msg_Ptr))
        bytes_to_read=strlen(msg_Ptr);

    /*
     * Actually transfer the data onto the userspace buffer.
     * For this task we use copy_to_user() due to security issues
     */
    if (copy_to_user(buffer,msg_Ptr,bytes_to_read))
        return -EFAULT;

    /* Update the pointer for the next read operation */
    msg_Ptr+=bytes_to_read;

    /*
     * The read operation returns the actual number of bytes
     * we copied  in the user's buffer
     */
    return bytes_to_read;
}

/*
 * Called when a process writes to dev file: echo "hi" > /dev/chardev
 */
static ssize_t device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
   int state;
   int leds;
   int i;
   int scroll_lock,num_lock,caps_lock;
   state=0;
   scroll_lock=0;
   num_lock=0;
   caps_lock=0;
   
   if (copy_from_user(kbuff,buff,len)) //Try to copy from user space
        return -EINVAL;

   for (i=0;i<len;i++){
      
	switch(kbuff[i]){
	case '3':
	scroll_lock = 1;
	break;
	case '2':					
	num_lock = 1;
	break;
	case '1':
	caps_lock = 1;
	break;
	default:
	break;

	}
  }
	leds=num_lock<<2|caps_lock<<1|scroll_lock;

	set_leds(kbd_driver,leds);
   
 
return len;

}

module_init( chardev_leds_init );
module_exit( chardev_leds_clean );

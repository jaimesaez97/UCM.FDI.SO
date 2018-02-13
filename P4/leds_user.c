#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <asm-generic/errno.h>
#include <sys/types.h>
#include <fcntl.h> 


int circular(int, int);
char* char_conversion (int);


int circular (int orig, int fd)
{
	int cont = 0;
	int i;
	
	printf("Recorrido de la ruta \n");
	for (i = orig; i < 3; i++){	
		if (!write(fd, char_conversion(i), 3))
			return -1;
		printf("Parada en la estacion %i \n",i);
		cont++;
		sleep (1);
	}
		
	return cont;
}

char* char_conversion (int n){
	switch (n){
		case 0: return "003";
		break;
		case 1: return "002";
		break;
		case 2: return "001";
		break;
	}
	return "";
}


int main (void)
{
	int i= 0;
	int fd = open("/dev/chardev_leds", O_RDWR);
	
	if (fd < 0)
		return -ENODEV;

	printf ("Circular \n");

	int num;

	printf ("Cuantas veces quieres hacer el recorrido? \n");
	if (!scanf ("%d", &num))
		return -EINVAL;

	while (i<num){
			circular(0, fd);
			i++;
	}

	if (!close(fd))
		return -ENODEV;

	return 0;
}

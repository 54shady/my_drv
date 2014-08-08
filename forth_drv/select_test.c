#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/select.h>

/* 
 *thirddrvtest 
 */
/*
 struct timeval {
               long    tv_sec;          seconds 
               long    tv_usec;         microseconds 
           };
*/
int main(int argc, char **argv)
{
	int fd;
	unsigned char key_val;
	fd_set read_fds;
	int retval;

	/* 不能定义为 *timeout 这是为什么 */
	struct timeval timeout;
	timeout.tv_sec  = 5;
	timeout.tv_usec = 0;

	fd = open("/dev/buttons", O_RDWR);
	if (fd < 0)
	{
		printf("can't open!\n");
		return -1;
	}

	FD_ZERO(&read_fds);
	FD_SET(fd, &read_fds);

	while (1)
	{
		retval = select(1, &read_fds, NULL, NULL, &timeout);

		if (retval == -1)
		{
			perror("select()\n");
		}
		else if (retval)
		{
			read(fd, &key_val, 1);
			printf("key_val = 0x%x\n", key_val);
		}
		else 
		{
			printf("5 seconds time out\n");
		}
	}

	close(fd);
	
	return 0;
}


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <poll.h>

/* 
 *thirddrvtest 
 */
int main(int argc, char **argv)
{
	int fd;
	unsigned char key_val;
	struct pollfd fds[1];	
		
	fd = open("/dev/buttons", O_RDWR);
	if (fd < 0)
	{
		printf("can't open!\n");
		return -1;
	}

	fds[0].fd     = fd;
	fds[0].events = POLLIN; 

	while (1)
	{
		if (!poll(fds, 1, 5000))
		{
			printf("time out\n");
		}
		else
		{
			read(fd, &key_val, 1);
			printf("key_val = 0x%x\n", key_val);
		}
	}

	close(fd);
	
	return 0;
}


#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

void print_usage(char *file)
{
	printf("Usage:\n");
	printf("%s <on | off>\n", file);
}

int main(int argc, char **argv)
{
	int val;
	int fd;
	if (argc != 2)
	{
		print_usage(argv[0]);
		return -1;
	}

	if (strcmp(argv[1], "on") == 0)
		val = 1;
	else if (strcmp(argv[1], "off") == 0)
		val = 0;
	else
		print_usage(argv[0]);
	
	fd = open("/dev/xxx", O_RDWR);
	if (fd < 0 )
	{
		printf("can't open\n");
		return -1;
	}

	write(fd, &val, 4);

	close(fd);
	return 0;
}

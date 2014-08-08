#include <stdio.h>
#include <fcntl.h>
/*
 * ./test <dev>
 * */

void print_usage(const char *name)
{
	printf("Usage:\n");
	printf("%s <dev> \n", name);
}

int main(int argc, char **argv)
{
	int fd;
	if (argc != 2)
	{
		print_usage(argv[0]);
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0)
	{
		printf("Oops.....can't open it\n");
		return -1;
	}

	close(fd);
	return 0;
}


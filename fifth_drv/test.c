#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <unistd.h>

int fd;

void my_sighandler(int signum)
{
	unsigned char key_val;
	read(fd, &key_val, 1);
	printf("key_val = 0x%x\n", key_val);
}
int main(int argc, char **argv)
{
	signal(SIGIO, my_sighandler);

	int flags;
	
	fd = open("/dev/buttons", O_RDWR);
	if (fd < 0)
	{
		printf("can open /dev/buttons\n");
		return -1;
	}

	/* ��������������źŷ�����ǰӦ�ó��� */
	fcntl(fd, F_SETOWN, getpid());

	/* �����ļ���flags����Ϊ FASYNC */
	/* �Ȼ�ȡ�ļ���flags */
	flags = fcntl(fd, F_GETFL);
	
	/* ���flags FASYNC���� �����ͻ����������fasync���� */
	fcntl(fd, F_SETFL, flags | FASYNC);
	
	while (1)
	{
		sleep(50000);
	}

	close(fd);
	
	return 0;
}

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/irq.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>

static struct class *fifth_drv_class;
static struct class_device	*fifth_drv_class_dev;
static DECLARE_WAIT_QUEUE_HEAD(fifth_waitqueue);
/* �ж��¼���־, �жϷ����������1��fifth_drv_read������0 */
static volatile int ev_press = 0;
static struct fasync_struct *fifth_async_queue;


struct pin_desc{
	unsigned int pin;
	unsigned int key_val;
};

static unsigned char key_val;
/*
 * EINT8 --GPG0--k1-->52
 * EINT11--GPG3--k2-->55
 * EINT13--GPG5--k3-->57
 * EINT14--GPG6--k4-->58
 * EINT15--GPG7--k5-->59
 * EINT19--GPG11--k6-->63
 */
/* ��ֵ: ����ʱ�ֱ��Զ���Ϊ: 0x01, 0x02, 0x03, 0x04 ,0x05, 0x06 */
/* ��ֵ: �ɿ�ʱ�ֱ��Զ���Ϊ: 0x81, 0x82, 0x83, 0x84 ,0x85, 0x86 */

struct pin_desc pindesc[6] = {
		{S3C2410_GPG0,  0x01},
		{S3C2410_GPG3,  0x02},
		{S3C2410_GPG5,  0x03},
		{S3C2410_GPG6,  0x04},
		{S3C2410_GPG7,  0x05},
		{S3C2410_GPG11, 0x06},
};


static irqreturn_t fifth_irq_handle(int irq, void *dev_id)
{
	struct pin_desc *pindesc = (struct pin_desc*)dev_id;
	unsigned int pinVal;

	/* ��ȡ����ֵ */
	pinVal = s3c2410_gpio_getpin(pindesc->pin);

	/* ȷ������ֵ�Ǹߵ�ƽ���ǵ͵�ƽ */
	if (pinVal)
	{
		/* pin ���Ǹߵ�ƽʱ���������ɿ� */
		key_val = 0x80 | pindesc->key_val;
	}
	else
	{
		/* pin ���ǵ͵�ƽʱ������������ */
		key_val = pindesc->key_val;
	}


	/* ������Ϊ�������첽֪ͨ�Ļ�����Ҫ���߽���
	 * ֮ǰ��Ҫ�õ���������Ϊ��Ӧ�ó����в��ϵ�ȥ��ȡ�Ƿ��а�������
	 * �������첽֪ͨ�Ļ��ǰ����Ѿ����»��ɿ��˲ŷ��źŸ�Ӧ�ó���
	 */
	//ev_press = 1;
    //wake_up_interruptible(&fifth_waitqueue);   /* �������ߵĽ��� */

	/* ��fifth_async_queue�������õĽ��̷��ź� */
	kill_fasync(&fifth_async_queue, SIGIO, POLL_IN);

	return IRQ_HANDLED;
}

static int fifth_drv_open(struct inode *inode, struct file *file)
{
	/*
	 * EINT8 --GPG0--k1-->52
	 * EINT11--GPG3--k2-->55
	 * EINT13--GPG5--k3-->57
	 * EINT14--GPG6--k4-->58
	 * EINT15--GPG7--k5-->59
	 * EINT19--GPG11--k6-->63
	 */
	request_irq(IRQ_EINT8,  fifth_irq_handle, IRQT_BOTHEDGE, "k1", &pindesc[0]);
	request_irq(IRQ_EINT11, fifth_irq_handle, IRQT_BOTHEDGE, "k2", &pindesc[1]);
	request_irq(IRQ_EINT13, fifth_irq_handle, IRQT_BOTHEDGE, "k3", &pindesc[2]);
	request_irq(IRQ_EINT14, fifth_irq_handle, IRQT_BOTHEDGE, "k4", &pindesc[3]);
	request_irq(IRQ_EINT15, fifth_irq_handle, IRQT_BOTHEDGE, "k5", &pindesc[4]);
	request_irq(IRQ_EINT19, fifth_irq_handle, IRQT_BOTHEDGE, "k6", &pindesc[5]);

	return 0;
}

ssize_t fifth_drv_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{

	if (size != 1)
	return -EINVAL;

	/* ���û�а������µĻ����򽫷��뵽fifth_waitqueue�� */
	/* ev_press ��ʼֵΪ0 �������� */
	/* ���а�������ʱ�����жϣ������жϴ�����
	 * ���жϴ������л���fifth_waitqueue�����ߵĽ���
	 */
	 /* ֮ǰ�ǲ�֪���Ƿ��а�����������Ҫ����
      * �������Ѿ��а��������˲��յ��ź�ȥ������ֵ�ģ����Բ���Ҫ����
	  */
	//wait_event_interruptible(fifth_waitqueue, ev_press);

	if (copy_to_user(buf, &key_val, 1))
		return -EINVAL;

	//ev_press = 0;
	return 1;
}

static int fifth_drv_release(struct inode *inode, struct file *file)
{
	printk("driver release being called\n");
	
	free_irq(IRQ_EINT8,  &pindesc[0]);
	free_irq(IRQ_EINT11, &pindesc[1]);
	free_irq(IRQ_EINT13, &pindesc[2]);
	free_irq(IRQ_EINT14, &pindesc[3]);
	free_irq(IRQ_EINT15, &pindesc[4]);
	free_irq(IRQ_EINT19, &pindesc[5]);

	return 0;
}

static unsigned int fifth_drv_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int    mask = 0;

	printk("driver poll is being call\n");
	
	poll_wait(file, &fifth_waitqueue, wait);
	
	if (ev_press)
		mask |= POLLIN | POLLRDNORM;
	
	return mask;
}

static int fifth_drv_fasync(int fd, struct file *file, int on)
{

	printk("driver fasync is being called\n");
	if (fasync_helper(fd, file, on, &fifth_async_queue) >= 0)
		return 0;
	else
		return -EIO;
}

static struct file_operations fifth_drv_fops = {
	.owner   =  THIS_MODULE,
	.open    =  fifth_drv_open,     
	.read	 =	fifth_drv_read,	
	.release =  fifth_drv_release,
	.poll    =  fifth_drv_poll,
	.fasync  =  fifth_drv_fasync,
};


int major;
static int fifth_drv_init(void)
{
	major = register_chrdev(0, "fifth_drv", &fifth_drv_fops);
	fifth_drv_class = class_create(THIS_MODULE, "fifth_drv");

	fifth_drv_class_dev = class_device_create(fifth_drv_class, NULL, MKDEV(major, 0), NULL, "buttons"); /* /dev/buttons */

	return 0;
}

static void fifth_drv_exit(void)
{
	unregister_chrdev(major, "fifth_drv");
	class_device_unregister(fifth_drv_class_dev);
	class_destroy(fifth_drv_class);
}

module_init(fifth_drv_init);
module_exit(fifth_drv_exit);
MODULE_LICENSE("GPL");

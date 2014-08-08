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

static struct class *forth_drv_class;
static struct class_device	*forth_drv_class_dev;
static DECLARE_WAIT_QUEUE_HEAD(forth_waitqueue);
/* 中断事件标志, 中断服务程序将它置1，forth_drv_read将它清0 */
static volatile int ev_press = 0;


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
/* 键值: 按下时分别自定义为: 0x01, 0x02, 0x03, 0x04 ,0x05, 0x06 */
/* 键值: 松开时分别自定义为: 0x81, 0x82, 0x83, 0x84 ,0x85, 0x86 */

struct pin_desc pindesc[6] = {
		{S3C2410_GPG0,  0x01},
		{S3C2410_GPG3,  0x02},
		{S3C2410_GPG5,  0x03},
		{S3C2410_GPG6,  0x04},
		{S3C2410_GPG7,  0x05},
		{S3C2410_GPG11, 0x06},
};


static irqreturn_t forth_irq_handle(int irq, void *dev_id)
{
	struct pin_desc *pindesc = (struct pin_desc*)dev_id;
	unsigned int pinVal;

	/* 读取引脚值 */
	pinVal = s3c2410_gpio_getpin(pindesc->pin);

	/* 确定引脚值是高电平还是低电平 */
	if (pinVal)
	{
		/* pin 脚是高电平时表明按键松开 */
		key_val = 0x80 | pindesc->key_val;
	}
	else
	{
		/* pin 脚是低电平时表明按键按下 */
		key_val = pindesc->key_val;
	}

	ev_press = 1;
    wake_up_interruptible(&forth_waitqueue);   /* 唤醒休眠的进程 */

	return IRQ_HANDLED;
}

static int forth_drv_open(struct inode *inode, struct file *file)
{
	/*
	 * EINT8 --GPG0--k1-->52
	 * EINT11--GPG3--k2-->55
	 * EINT13--GPG5--k3-->57
	 * EINT14--GPG6--k4-->58
	 * EINT15--GPG7--k5-->59
	 * EINT19--GPG11--k6-->63
	 */
	request_irq(IRQ_EINT8,  forth_irq_handle, IRQT_BOTHEDGE, "k1", &pindesc[0]);
	request_irq(IRQ_EINT11, forth_irq_handle, IRQT_BOTHEDGE, "k2", &pindesc[1]);
	request_irq(IRQ_EINT13, forth_irq_handle, IRQT_BOTHEDGE, "k3", &pindesc[2]);
	request_irq(IRQ_EINT14, forth_irq_handle, IRQT_BOTHEDGE, "k4", &pindesc[3]);
	request_irq(IRQ_EINT15, forth_irq_handle, IRQT_BOTHEDGE, "k5", &pindesc[4]);
	request_irq(IRQ_EINT19, forth_irq_handle, IRQT_BOTHEDGE, "k6", &pindesc[5]);

	return 0;
}

ssize_t forth_drv_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{

	if (size != 1)
	return -EINVAL;

	/* 如果没有按键按下的话程序将放入到forth_waitqueue中 */
	/* ev_press 初始值为0 进入休眠 */
	/* 当有按键按下时产生中断，调用中断处理函数
	 * 在中断处理函数中唤醒forth_waitqueue中休眠的进程
	 */
	wait_event_interruptible(forth_waitqueue, ev_press);

	if (copy_to_user(buf, &key_val, 1))
		return -EINVAL;

	ev_press = 0;
	return 1;
}

static int forth_drv_release(struct inode *inode, struct file *file)
{
	free_irq(IRQ_EINT8,  &pindesc[0]);
	free_irq(IRQ_EINT11, &pindesc[1]);
	free_irq(IRQ_EINT13, &pindesc[2]);
	free_irq(IRQ_EINT14, &pindesc[3]);
	free_irq(IRQ_EINT15, &pindesc[4]);
	free_irq(IRQ_EINT19, &pindesc[5]);

	return 0;
}

static unsigned int forth_drv_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int    mask = 0;
printk("driver poll is being call\n");
	poll_wait(file, &forth_waitqueue, wait);
	
	if (ev_press)
		mask |= POLLIN | POLLRDNORM;
	return mask;
}

static struct file_operations forth_drv_fops = {
	.owner   =  THIS_MODULE,
	.open    =  forth_drv_open,     
	.read	 =	forth_drv_read,	
	.release =  forth_drv_release,
	.poll    =  forth_drv_poll,
};


int major;
static int forth_drv_init(void)
{
	major = register_chrdev(0, "forth_drv", &forth_drv_fops);
	forth_drv_class = class_create(THIS_MODULE, "forth_drv");

	forth_drv_class_dev = class_device_create(forth_drv_class, NULL, MKDEV(major, 0), NULL, "buttons"); /* /dev/buttons */

	return 0;
}

static void forth_drv_exit(void)
{
	unregister_chrdev(major, "forth_drv");
	class_device_unregister(forth_drv_class_dev);
	class_destroy(forth_drv_class);
}

module_init(forth_drv_init);
module_exit(forth_drv_exit);
MODULE_LICENSE("GPL");

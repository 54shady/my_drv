#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>
#include <linux/device.h>

static struct class *firstdrv_class;
static struct class_device	*firstdrv_class_dev;

/* 原理图中4个LED都是用的GPB管脚 */
volatile unsigned long *gpbcon = NULL;
volatile unsigned long *gpbdat = NULL;

static int first_drv_open(struct inode *inode, struct file *file)
{
	/* 先清0 */
	*gpbcon &= ~((0x3<<(5*2)) | (0x3<<(6*2)) | (0x3<<(7*2)) | (0x3<<(8*2)));
	/* 5,6,7,8 设置成输出管脚 */
	*gpbcon |= ((0x1<<(5*2)) | (0x1<<(6*2)) | (0x1<<(7*2)) | (0x1<<(8*2)));

	return 0;
}

static ssize_t first_drv_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	int val;
	int copy_size;
	copy_size = copy_from_user(&val, buf, count);

	if (val == 1)
	{
		/* 全亮 */
		*gpbdat &= ~((1<<5) | (1<<6) | (1<<7) | (1<<8));
	}
	else
	{
		/*全灭 */
		*gpbdat |= (1<<5) | (1<<6) | (1<<7) | (1<<8);
	}
	return 0;
}
static struct file_operations first_drv_fops = {
	.owner  =   THIS_MODULE,  
	.open   =   first_drv_open,     
	.write	=	first_drv_write,	   
};

int major;
static int first_drv_init(void)
{
	major = register_chrdev(0, "first_drv", &first_drv_fops);

	firstdrv_class = class_create(THIS_MODULE, "firstdrv");

	firstdrv_class_dev = class_device_create(firstdrv_class, NULL, MKDEV(major, 0), NULL, "xxx");/*  /dev/xxx   */

	gpbcon = (volatile unsigned long *)ioremap(0x56000010, 16);/*映射大小为什么是16*/
	gpbdat = gpbcon + 1;

	return 0;
}

static void first_drv_exit(void)
{
	unregister_chrdev(major, "first_drv");
	class_device_unregister(firstdrv_class_dev);
	class_destroy(firstdrv_class);
	iounmap(gpbcon);
}

module_init(first_drv_init);
module_exit(first_drv_exit);

MODULE_LICENSE("GPL");

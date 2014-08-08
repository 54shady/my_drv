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

static struct class *second_drv_class;
static struct class_device	*second_drv_class_dev;

volatile unsigned long *gpgcon;
volatile unsigned long *gpgdat;

static int second_drv_open(struct inode *inode, struct file *file)
{
	/*
	 * EINT8 --GPG0--k1
	 * EINT11--GPG3--k2
	 * EINT13--GPG5--k3
	 * EINT14--GPG6--k4
	 * EINT15--GPG7--k5
	 * EINT19--GPG11--k6
	 */
	/* 设置成输入引脚 */
	*gpgcon &= ~((0x3<<(0*2)) | (0x3<<(3*2)) | (0x3<<(5*2)) | (0x3<<(6*2) | (0x3<<(7*2) | (0x3<<(11*2)))));

	return 0;
}

ssize_t second_drv_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	/* 返回6个引脚的电平 */
	unsigned char key_vals[6];
	int regval;

	if (size != sizeof(key_vals))
	return -EINVAL;

	regval = *gpgdat;
	key_vals[0] = (regval & (1<<0)) ? 1 : 0;
	key_vals[1] = (regval & (1<<3)) ? 1 : 0;
	key_vals[2] = (regval & (1<<5)) ? 1 : 0;
	key_vals[3] = (regval & (1<<6)) ? 1 : 0;
	key_vals[4] = (regval & (1<<7)) ? 1 : 0;
	key_vals[5] = (regval & (1<<11)) ? 1 : 0;

	if (copy_to_user(buf, key_vals, sizeof(key_vals)) == 0)
		return 0;

	return sizeof(key_vals);
}

static struct file_operations second_drv_fops = {
	.owner  =   THIS_MODULE,
	.open   =   second_drv_open,     
	.read	=	second_drv_read,	   
};


int major;
static int second_drv_init(void)
{
	major = register_chrdev(0, "second_drv", &second_drv_fops);
	second_drv_class = class_create(THIS_MODULE, "second_drv");

	second_drv_class_dev = class_device_create(second_drv_class, NULL, MKDEV(major, 0), NULL, "buttons"); /* /dev/buttons */

	gpgcon = (volatile unsigned long *)ioremap(0x56000060, 16);
	gpgdat = gpgcon + 1;

	return 0;
}

static void second_drv_exit(void)
{
	unregister_chrdev(major, "second_drv");
	class_device_unregister(second_drv_class_dev);
	class_destroy(second_drv_class);
	iounmap(gpgcon);
}

module_init(second_drv_init);
module_exit(second_drv_exit);
MODULE_LICENSE("GPL");

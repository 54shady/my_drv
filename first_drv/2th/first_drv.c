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



static int first_drv_open(struct inode *inode, struct file *file)
{
	printk("first_drv_open\n");
	return 0;
}

static ssize_t first_drv_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	printk("first_drv_write\n");
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

	return 0;
}

static void first_drv_exit(void)
{
	unregister_chrdev(major, "first_drv");
	class_device_unregister(firstdrv_class_dev);
	class_destroy(firstdrv_class);
}

module_init(first_drv_init);
module_exit(first_drv_exit);


MODULE_LICENSE("GPL");

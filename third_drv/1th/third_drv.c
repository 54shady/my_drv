#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>
#include <linux/device.h>

static struct class *third_drv_class;
static struct class_device	*third_drv_class_dev;

volatile unsigned long *gpgcon;
volatile unsigned long *gpgdat;

static irqreturn_t third_irq_handle(int irq, void *dev_id)
{
	printk("irq = %d\n", irq);
	
	return IRQ_HANDLED;
}

static int third_drv_open(struct inode *inode, struct file *file)
{
	/*
	 * EINT8 --GPG0--k1-->52
	 * EINT11--GPG3--k2-->55
	 * EINT13--GPG5--k3-->57
	 * EINT14--GPG6--k4-->58
	 * EINT15--GPG7--k5-->59
	 * EINT19--GPG11--k6-->63
	 */
	request_irq(IRQ_EINT8,  third_irq_handle, IRQT_BOTHEDGE, "k1", 1);
	request_irq(IRQ_EINT11, third_irq_handle, IRQT_BOTHEDGE, "k2", 1);
	request_irq(IRQ_EINT13, third_irq_handle, IRQT_BOTHEDGE, "k3", 1);
	request_irq(IRQ_EINT14, third_irq_handle, IRQT_BOTHEDGE, "k4", 1);
	request_irq(IRQ_EINT15, third_irq_handle, IRQT_BOTHEDGE, "k5", 1);
	request_irq(IRQ_EINT19, third_irq_handle, IRQT_BOTHEDGE, "k6", 1);

	return 0;
}

ssize_t third_drv_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
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

static int third_drv_release(struct inode *inode, struct file *file)
{
	free_irq(IRQ_EINT8,  1);
	free_irq(IRQ_EINT11, 1);
	free_irq(IRQ_EINT13, 1);
	free_irq(IRQ_EINT14, 1);
	free_irq(IRQ_EINT15, 1);
	free_irq(IRQ_EINT19, 1);

	return 0;
}
static struct file_operations third_drv_fops = {
	.owner   =  THIS_MODULE,
	.open    =  third_drv_open,     
	.read	 =	third_drv_read,	
	.release   =  third_drv_release,
};


int major;
static int third_drv_init(void)
{
	major = register_chrdev(0, "third_drv", &third_drv_fops);
	third_drv_class = class_create(THIS_MODULE, "third_drv");

	third_drv_class_dev = class_device_create(third_drv_class, NULL, MKDEV(major, 0), NULL, "buttons"); /* /dev/buttons */

	gpgcon = (volatile unsigned long *)ioremap(0x56000060, 16);
	gpgdat = gpgcon + 1;

	return 0;
}

static void third_drv_exit(void)
{
	unregister_chrdev(major, "third_drv");
	class_device_unregister(third_drv_class_dev);
	class_destroy(third_drv_class);
	iounmap(gpgcon);
}

module_init(third_drv_init);
module_exit(third_drv_exit);
MODULE_LICENSE("GPL");

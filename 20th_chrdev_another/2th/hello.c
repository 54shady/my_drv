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
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define HELLO_COUNT	2
static int major;
static struct cdev hello_cdev;
static struct cdev hello2_cdev;
static struct class *cls;

static int hello_open(struct inode *inode, struct file *file)
{
	printk("hello open\n");
	
	return 0;
}

static int hello2_open(struct inode *inode, struct file *file)
{
	printk("hello  -2- open\n");
	
	return 0;
}

static const struct file_operations hello_fops = {
	.owner	= THIS_MODULE,
	.open	= hello_open,
};

static const struct file_operations hello2_fops = {
	.owner	= THIS_MODULE,
	.open	= hello2_open,
};

static int hello_init(void)
{
	dev_t	dev_id;

	/* 只有(major, 0~1)对应hello_fops  所以无法打开hello2 */
	if (major) {
		dev_id = MKDEV(major, 0);
		register_chrdev_region(dev_id, HELLO_COUNT, "hello");
	} else {
		alloc_chrdev_region(&dev_id, 0, HELLO_COUNT, "hello");
		major = MAJOR(dev_id);
	}

	cdev_init(&hello_cdev, &hello_fops);
	cdev_add(&hello_cdev, dev_id, HELLO_COUNT);

	/* 再创建一个/dev/hello3  此设备号为3并给其分配一个新的file_ops */
	dev_id = MKDEV(major, 3);
	register_chrdev_region(dev_id, 1, "hello");
	cdev_init(&hello2_cdev, &hello2_fops);
	cdev_add(&hello2_cdev, dev_id, 1);

	
	cls = class_create(THIS_MODULE, "hello");
	class_device_create(cls, NULL, MKDEV(major, 0), NULL, "hello0");/* /dev/hello0 */
	class_device_create(cls, NULL, MKDEV(major, 1), NULL, "hello1");/* /dev/hello1 */
	class_device_create(cls, NULL, MKDEV(major, 2), NULL, "hello2");/* /dev/hello2 */
	class_device_create(cls, NULL, MKDEV(major, 3), NULL, "hello3");/* /dev/hello3 */
	
	return 0;
}

static void hello_exit(void)
{
	class_device_destroy(cls, MKDEV(major, 0));
	class_device_destroy(cls, MKDEV(major, 1));
	class_device_destroy(cls, MKDEV(major, 2));
	class_device_destroy(cls, MKDEV(major, 3));

	class_destroy(cls);

	cdev_del(&hello_cdev);
	cdev_del(&hello2_cdev);
	
	unregister_chrdev_region(MKDEV(major, 0), HELLO_COUNT);
	unregister_chrdev_region(MKDEV(major, 3), 1);

}

module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");

#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

static int major;
static volatile unsigned long *gpio_con;
static volatile unsigned long *gpio_dat;
static int pin;
static struct class *led_class;
static struct class_device	*led_class_device;

static int led_open(struct inode *inode, struct file *file)
{
	*gpio_con &= ~(0x3<<(pin*2));
	*gpio_con |= (0x1<<(pin*2));

	return 0;
}

static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t * ppos)
{
	int val;
	int copy_size;
	copy_size = copy_from_user(&val, buf, count);

	if (val == 1)
		*gpio_dat &= ~(1<<pin);
	else
		*gpio_dat |= (1<<pin);

	return 0;
}

static struct file_operations led_fops = {
	.owner  =   THIS_MODULE,  
	.open   =   led_open,     
	.write	=	led_write,	   
};

static int led_probe(struct platform_device *dev)
{
	printk("led_probe is being called\n");
	
	/* 根据platform_device的资源进行操作 */	
	struct resource *res;

	/* 第三个参数表示IORESOURCE_MEM类资源的第几个 */
	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	gpio_con = (volatile unsigned long *)ioremap(res->start, res->end - res->start);
	gpio_dat = gpio_con + 1;

	res = platform_get_resource(dev, IORESOURCE_IRQ, 0);
	/* 获取引脚 */
	pin = res->start;

	/* 注册字符设备驱动程序 */
	major = register_chrdev(0,"myled", &led_fops);

	led_class = class_create(THIS_MODULE, "myled");

	led_class_device = class_device_create(led_class, NULL, MKDEV(major, 0), NULL, "led");/*  /dev/led   */

	return 0;
}

static int led_remove(struct platform_device *dev)
{
	printk("led_remove is being called\n");

	/* 卸载字符设备驱动程序 */
	class_device_destroy(led_class, MKDEV(major, 0));
	class_destroy(led_class);
	unregister_chrdev(major, "myled");
	iounmap(gpio_con);

	return 0;
}

/* 分配/设置/注册一个platform_driver */
static struct platform_driver led_platform_driver = {
	.probe		= led_probe,
	.remove		= led_remove,
	.driver		= {
		.name	= "myled",
	},
};

static int led_drv_init(void)
{
	platform_driver_register(&led_platform_driver);
	
	return 0;
}

static void led_drv_exit(void)
{
	platform_driver_unregister(&led_platform_driver);
}

module_init(led_drv_init);
module_exit(led_drv_exit);
MODULE_LICENSE("GPL");

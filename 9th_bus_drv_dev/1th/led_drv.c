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

static int led_probe(struct platform_device *dev)
{
	printk("led_probe is being called\n");
	return 0;
}

static int led_remove(struct platform_device *dev)
{
	printk("led_remove is being called\n");
	
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

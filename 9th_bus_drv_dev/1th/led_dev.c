#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

static void led_release(struct device * dev)
{
}

/* 分配/设置/注册一个platform_device */
static struct platform_device led_platform_device = {
	.name			= "myled",
	.dev			= {
		.release	= led_release,
	},
};

static int led_dev_init(void)
{
	platform_device_register(&led_platform_device);

	return 0;
}

static void led_dev_exit(void)
{
	platform_device_unregister(&led_platform_device);
}

module_init(led_dev_init);
module_exit(led_dev_exit);
MODULE_LICENSE("GPL");

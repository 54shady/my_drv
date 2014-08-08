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

static struct resource led_resource[] = {
    [0] = {
 		.start = 0x56000010,   /* MINI2440的LED是GPB5,6,7,8, GPBCON地址是0x56000010 */
        .end   = 0x56000010 + 4,/* 这里我还是按照之前的写的 */
        .flags = IORESOURCE_MEM,
    },
    [1] = {
    	 /* GPB5 表示第一个LED1 */
		 /*
        .start = 5,                     
        .end   = 5,
        .flags = IORESOURCE_IRQ,
        */
    	 /* GPB6 表示第一个LED2 */
      //  .start = 6,                     
       // .end   = 6,
   //     .flags = IORESOURCE_IRQ,
    	 /* GPB7 表示第一个LED3 */
     //   .start = 7,                     
       // .end   = 7,
        //.flags = IORESOURCE_IRQ,
    	 /* GPB8 表示第一个LED4 */
        .start = 8,                     
        .end   = 8,
        .flags = IORESOURCE_IRQ,
    }
};

/* 分配/设置/注册一个platform_device */
static struct platform_device led_platform_device = {
	.name			= "myled",
    .num_resources    = ARRAY_SIZE(led_resource),
    .resource     = led_resource,
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

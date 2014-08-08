
/* �ο� drivers\input\keyboard\gpio_keys.c */

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

#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>

struct pin_desc{
	int irq;
	char *name;
	unsigned int pin;
	unsigned int key_val;
};

struct pin_desc pindesc[6] = {
	{IRQ_EINT8,  "K1", S3C2410_GPG0,  KEY_L},
	{IRQ_EINT11, "K2", S3C2410_GPG3,  KEY_S},
	{IRQ_EINT13, "K3", S3C2410_GPG5,  KEY_ENTER},
	{IRQ_EINT14, "K4", S3C2410_GPG6,  KEY_C},
	{IRQ_EINT15, "K5", S3C2410_GPG7,  KEY_A},
	{IRQ_EINT19, "K6", S3C2410_GPG11, KEY_T},
};

static struct input_dev *buttons_input_dev;
static struct timer_list buttons_timer;
static struct pin_desc *irq_pd;

static irqreturn_t buttons_irq(int irq, void *dev_id)
{
	/* 10ms��������ʱ�� */
	irq_pd = (struct pin_desc *)dev_id;
	mod_timer(&buttons_timer, jiffies+HZ/100);
	return IRQ_RETVAL(IRQ_HANDLED);
}

static void buttons_timer_function(unsigned long data)
{
	struct pin_desc * pindesc = irq_pd;
	unsigned int pinval;

	if (!pindesc)
		return;
	
	pinval = s3c2410_gpio_getpin(pindesc->pin);

	if (pinval)
	{
		/* �ɿ� : ���һ������: 0-�ɿ�, 1-���� */
		input_event(buttons_input_dev, EV_KEY, pindesc->key_val, 0);
		input_sync(buttons_input_dev);
	}
	else
	{
		/* ���� */
		input_event(buttons_input_dev, EV_KEY, pindesc->key_val, 1);
		input_sync(buttons_input_dev);
	}
}

static int buttons_init(void)
{
	int i;
	
	/* 1.����һ��input_dev �ṹ�� */
	buttons_input_dev = input_allocate_device();
	if (!buttons_input_dev)
		return -ENOMEM;
	
	/* 2.����input_dev */
	/* 2.1 �ܲ��������¼� */
	set_bit(EV_KEY, buttons_input_dev->evbit);

	/* �����ظ����¼� ��ס������ʱ����кܶఴ��ֵ */
	set_bit(EV_REP, buttons_input_dev->evbit);

	/* 2.2 �ܲ���������������Щ�¼�: L,S,ENTER,LEFTSHIT */
	set_bit(KEY_L, buttons_input_dev->keybit);
	set_bit(KEY_S, buttons_input_dev->keybit);
	set_bit(KEY_C, buttons_input_dev->keybit);
	set_bit(KEY_A, buttons_input_dev->keybit);
	set_bit(KEY_T, buttons_input_dev->keybit);
	set_bit(KEY_ENTER, buttons_input_dev->keybit);

	/* 3.ע�� input_register_devices */
	input_register_device(buttons_input_dev);

	/* 4.Ӳ����� */
	init_timer(&buttons_timer);
	buttons_timer.function = buttons_timer_function;
	add_timer(&buttons_timer);

	for (i = 0; i < 6; i++)
		if (request_irq(pindesc[i].irq, buttons_irq, IRQT_BOTHEDGE, pindesc[i].name, &pindesc[i]))
				return -EAGAIN;

	return 0;
}

static void buttons_exit(void)
{
	int i;
	for (i = 0; i < 6; i++)
		free_irq(pindesc[i].irq, &pindesc[i]);

	del_timer(&buttons_timer);
	input_unregister_device(buttons_input_dev);
	input_free_device(buttons_input_dev);
}

module_init(buttons_init);
module_exit(buttons_exit);

MODULE_LICENSE("GPL");


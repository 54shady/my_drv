#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/plat-s3c24xx/ts.h>

#include <asm/arch/regs-adc.h>
#include <asm/arch/regs-gpio.h>

struct s3c_ts_regs {
	unsigned long adccon;
	unsigned long adctsc;
	unsigned long adcdly;
	unsigned long adcdat0;
	unsigned long adcdat1;
	unsigned long adcupdn;
};

static struct input_dev *s3c_ts_input_dev;
static volatile struct s3c_ts_regs *s3c_ts_regs;
static struct timer_list ts_timer;

static void enter_wait_pen_down_mode(void)
{
	s3c_ts_regs->adctsc = 0xd3;
}
static void enter_wait_pen_up_mode(void)
{
	s3c_ts_regs->adctsc = 0x1d3;
}

static void enter_masure_xy_mode(void)
{
	s3c_ts_regs->adctsc = 1<<2;
}

static void start_adc_mode(void)
{
	s3c_ts_regs->adccon |= 1<<0;
}

static void s3c_ts_timer_function(unsigned long data)
{
	if (s3c_ts_regs->adcdat0 & (1<<15))
	{
		/* 已经松开 */
		input_report_abs(s3c_ts_input_dev, ABS_PRESSURE, 0);
		input_report_key(s3c_ts_input_dev, BTN_TOUCH, 0);
		input_sync(s3c_ts_input_dev);
		enter_wait_pen_down_mode();
	}
	else
	{
		/* 测量X/Y坐标 */
		enter_masure_xy_mode();
		start_adc_mode();
	}
}

static irqreturn_t pen_down_up_irq(int irq, void *dev_id)
{
	if (s3c_ts_regs->adcdat0 & (1<<15))
	{
		//printk("pen up\n");
		input_report_abs(s3c_ts_input_dev, ABS_PRESSURE, 0);
		input_report_key(s3c_ts_input_dev, BTN_TOUCH, 0);
		input_sync(s3c_ts_input_dev);
		enter_wait_pen_down_mode();
	}
	else
	{
		//printk("pen donw\n");
		enter_masure_xy_mode();
		start_adc_mode();
		//enter_wait_pen_up_mode();
	}
	
	return IRQ_HANDLED;
}
static irqreturn_t adc_irq(int irq, void *dev_id)
{
	static int cnt = 0;
	unsigned long adcdat0, adcdat1;
	static unsigned long x[4], y[4];
	
	/* 优化措施2: 如果ADC完成时, 发现触摸笔已经松开, 则丢弃此次结果 */
	adcdat0 = s3c_ts_regs->adcdat0;
	adcdat1 = s3c_ts_regs->adcdat1;

	if (s3c_ts_regs->adcdat0 & (1<<15))
	{
		/* 已经松开 */
		cnt = 0;
		input_report_abs(s3c_ts_input_dev, ABS_PRESSURE, 0);
		input_report_key(s3c_ts_input_dev, BTN_TOUCH, 0);
		input_sync(s3c_ts_input_dev);
		enter_wait_pen_down_mode();
	}
	else
	{
		/* 优化措施3: 多次测量求平均值 */
		x[cnt] = adcdat0 & 0x3ff;
		y[cnt] = adcdat1 & 0x3ff;
		++cnt;
		if (cnt == 4)
		{
//			printk("x = %lu, y = %lu\n", (x[0]+x[1]+x[2]+x[3])/4, (y[0]+y[1]+y[2]+y[3])/4);
				input_report_abs(s3c_ts_input_dev, ABS_X, (x[0]+x[1]+x[2]+x[3])/4);
				input_report_abs(s3c_ts_input_dev, ABS_Y, (y[0]+y[1]+y[2]+y[3])/4);
				input_report_abs(s3c_ts_input_dev, ABS_PRESSURE, 1);
				input_report_key(s3c_ts_input_dev, BTN_TOUCH, 1);
				input_sync(s3c_ts_input_dev);
			cnt = 0;
			enter_wait_pen_up_mode();

			mod_timer(&ts_timer, jiffies + HZ/100);
		}
		else
		{
			enter_masure_xy_mode();
			start_adc_mode();
		}		

	}
	
	return IRQ_HANDLED;
}
static int s3c_ts_init(void)
{
	struct clk *s3c_ts_clk;
	/* 1.分配一个input_dev结构体 */
	s3c_ts_input_dev = input_allocate_device();
	if (!s3c_ts_input_dev) {
		printk(KERN_ERR "Unable to allocate the input device !!\n");
		return -ENOMEM;
	}

	/* 2. 设置 */
	/* 2.1 能产生哪类事件 */
	set_bit(EV_KEY, s3c_ts_input_dev->evbit);
	set_bit(EV_ABS, s3c_ts_input_dev->evbit);

	/* 2.2 能产生这类事件里的哪些事件 */
	set_bit(BTN_TOUCH, s3c_ts_input_dev->keybit);

	input_set_abs_params(s3c_ts_input_dev, ABS_X, 0, 0x3FF, 0, 0);
	input_set_abs_params(s3c_ts_input_dev, ABS_Y, 0, 0x3FF, 0, 0);
	input_set_abs_params(s3c_ts_input_dev, ABS_PRESSURE, 0, 1, 0, 0);
	/* 3.注册 */
	input_register_device(s3c_ts_input_dev);
	
	/* 4.硬件相关的操作 */
	/* 4.1 使能时钟(CLKCON[15]) 开启ADC 模块 */
	s3c_ts_clk = clk_get(NULL, "adc");
	if (IS_ERR(s3c_ts_clk))
		return PTR_ERR(s3c_ts_clk);

	clk_enable(s3c_ts_clk);

	/* 4.2 设置S3C2440的ADC/TS寄存器 */
	s3c_ts_regs = ioremap(0x58000000, sizeof(struct s3c_ts_regs));
	if (!s3c_ts_regs) {
		printk(KERN_ERR " unable to remap memory\n");
		return -EAGAIN;
	}

	/* bit[14]=1   A/D converter prescaler enable
	 * bit[13:6]=49 A/D converter prescaler value,
	 *            ADCCLK=PCLK/(49+1)=50MHz/(49+1)=1MHz
	 * bit[0]=0 A/D conversion starts by enable. 先设为0
	 */
	 s3c_ts_regs->adccon = ((1<<14) | (49<<6) | (0<<0));

	if(request_irq(IRQ_TC, pen_down_up_irq, IRQF_SAMPLE_RANDOM, "ts_pen", NULL))
	{
		printk("request_irq error\n");
		return -EAGAIN;
	}

	if(request_irq(IRQ_ADC, adc_irq, IRQF_SAMPLE_RANDOM, "adc", NULL))
	{
		printk("request_irq error\n");
		return -EAGAIN;
	}

	/* 优化错施1: 
	 * 设置ADCDLY为最大值, 这使得电压稳定后再发出IRQ_TC中断
	 */
	s3c_ts_regs->adcdly = 0xffff;

	/* 优化措施5: 使用定时器处理长按,滑动的情况
	 * 
	 */
	init_timer(&ts_timer);
	ts_timer.function = s3c_ts_timer_function;
	add_timer(&ts_timer);

	enter_wait_pen_down_mode();

	return 0;
}

static void s3c_ts_exit(void)
{
	free_irq(IRQ_TC, NULL);
	free_irq(IRQ_ADC, NULL);
	iounmap(s3c_ts_regs);
	input_unregister_device(s3c_ts_input_dev);
	input_free_device(s3c_ts_input_dev);
	del_timer(&ts_timer);
}

module_init(s3c_ts_init);
module_exit(s3c_ts_exit);
MODULE_LICENSE("GPL");


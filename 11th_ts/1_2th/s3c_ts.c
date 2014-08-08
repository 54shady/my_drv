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
static struct s3c_ts_regs *s3c_ts_regs;

static void enter_wait_pen_down_mode(void)
{
	s3c_ts_regs->adctsc = 0xd3;
}
static void enter_wait_pen_up_mode(void)
{
	s3c_ts_regs->adctsc = 0x1d3;
}

static irqreturn_t pen_down_up_irq(int irq, void *dev_id)
{
	if (s3c_ts_regs->adcdat0 & (1<<15))
	{
		
		printk("pen up\n");
		enter_wait_pen_down_mode();
	}
	else
	{
		printk("pen donw\n");
		enter_wait_pen_up_mode();
	}
	
	return 0;
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

	/* 2.设置 */

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

	enter_wait_pen_down_mode();

	return 0;
}

static void s3c_ts_exit(void)
{
	free_irq(IRQ_TC, NULL);
	iounmap(s3c_ts_regs);
	input_unregister_device(s3c_ts_input_dev);
	input_free_device(s3c_ts_input_dev);
}

module_init(s3c_ts_init);
module_exit(s3c_ts_exit);
MODULE_LICENSE("GPL");


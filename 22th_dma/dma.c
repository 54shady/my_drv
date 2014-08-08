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
#include <linux/dma-mapping.h>
#include <linux/cdev.h>

/*
 * 在SDRAM中分别分配两块内存区域src,dst
 * 把src中的数据拷贝到dst中去
 * */
 /* 分配512K大小 */
#define BUF_SIZE	(512*1024) 
#define MEM_CPY_NO_DMA  0
#define MEM_CPY_DMA     1

#define DMA0_BASE_ADDR  0x4B000000
#define DMA1_BASE_ADDR  0x4B000040
#define DMA2_BASE_ADDR  0x4B000080
#define DMA3_BASE_ADDR  0x4B0000C0

struct dma_regs {
	unsigned long disrc;
	unsigned long disrcc;
	unsigned long didst;
	unsigned long didstc;
	unsigned long dcon;
	unsigned long dstat;
	unsigned long dcsrc;
	unsigned long dcdst;
	unsigned long dmasktrig;
};

static char *src;/* 源虚拟地址 */
static u32 src_phys; /* 源物理地址 */

static char *dst;/* 目的虚拟地址 */
static u32 dst_phys; /* 目的物理地址 */
static int major;
static struct cdev dma_cdev;
static struct class *cls;
static volatile struct dma_regs *dma_regs;

static DECLARE_WAIT_QUEUE_HEAD(dma_waitq);

/* 中断事件标志, 中断服务程序将它置1，ioctl将它清0 */
static volatile int ev_dma = 0;
static int irq_no;

static int dma_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int i;
	memset(src, 0xaa, BUF_SIZE);
	memset(dst, 0x55, BUF_SIZE);

	switch (cmd)
	{
		case MEM_CPY_NO_DMA:
		{
			for (i = 0; i < BUF_SIZE; i++)
				dst[i] = src[i];

			if (memcmp(src, dst, BUF_SIZE) == 0)
				printk("memcpy no dma ok\n");	
			else
				printk("memcpy no dma error\n");
			
			break;
		}
		
		case MEM_CPY_DMA:
		{
			ev_dma = 0;
			
			/* 设置DMA控制器 */
			/* 把源,目的,长度告诉DMA */
			dma_regs->disrc 	= src_phys;/* 源的物理地址 */
			dma_regs->disrcc 	= (0<<1) | (0<<0); /* 源位于AHB总线, 源地址递增 */
			dma_regs->didst 	= dst_phys; /* 目的的物理地址 */
			dma_regs->didstc 	= (0<<2) | (0<<1) | (0<<0); /* 目的位于AHB总线, 目的地址递增 */
			/* 使能中断,单个传输,软件触发等，具体看S3C2440A芯片手册 */
			dma_regs->dcon		= ((0<<31) | (1<<30) | (1<<29) | (0<<28) | (1<<27) | (0<<23) | (1<<22) | (0<<20) | (BUF_SIZE<<0));

			/* 启动DMA */
			dma_regs->dmasktrig = ((0<<2) | (1<<1) | (1<<0));

			/* 如何知道DMA什么时候完成? */
			/* 休眠 */
			wait_event_interruptible(dma_waitq, ev_dma);
			
			if (memcmp(src, dst, BUF_SIZE) == 0)
				printk("memcpy dma ok\n");	
			else
				printk("memcpy dma error\n");
			
			break;
		}
			
	}

	return 0;
}

static const struct file_operations dma_fops = {
	.owner	= THIS_MODULE,
	.ioctl	= dma_ioctl,
};

static irqreturn_t s3c_dma_irq(int irq, void *devid)
{
	/* 唤醒 */
	ev_dma = 1;
    wake_up_interruptible(&dma_waitq);   /* 唤醒休眠的进程 */
	return IRQ_HANDLED;
}

static int dma_init(void)
{
 	dev_t	dev_id;
	
	src = dma_alloc_writecombine(NULL, BUF_SIZE, &src_phys, GFP_KERNEL);
	if (NULL == src)
	{
		printk("alloc src error\n");
		return -ENOMEM;
	}

	dst = dma_alloc_writecombine(NULL, BUF_SIZE, &dst_phys, GFP_KERNEL);
	if (NULL == dst)
	{
		/* 目的内存分配失败了，把之前分配的源内存释放 */
		dma_free_writecombine(NULL, BUF_SIZE, src, src_phys);
		printk("alloc dst error\n");
		return -ENOMEM;
	}

	if (major) {
		dev_id = MKDEV(major, 0);
		register_chrdev_region(dev_id, 1, "dma");
	} else {
		alloc_chrdev_region(&dev_id, 0, 1, "dma");
		major = MAJOR(dev_id);
	}

	cdev_init(&dma_cdev, &dma_fops);
	cdev_add(&dma_cdev, dev_id, 1);

	cls = class_create(THIS_MODULE, "dma");
	class_device_create(cls, NULL, MKDEV(major, 0), NULL, "dma");/* /dev/dma */

	dma_regs = ioremap(DMA3_BASE_ADDR, sizeof(struct dma_regs));
	if (!dma_regs) {
		printk(KERN_ERR "unable to remap memory\n");
		return -EAGAIN;
	}

	/* DMA可用中断号IRQ_DMA0~IRQ_DMA3 */
	for (irq_no = IRQ_DMA0; irq_no <= IRQ_DMA3; irq_no++)
	{
		/* 申请中断成功退出循环 */
		if(!request_irq(irq_no, s3c_dma_irq, 0, "s3c_dma", 1))
			break;

		/* 申请到DMA3还没成功 提示错误 */
		if (irq_no == IRQ_DMA3)
		{
			printk("can't request_irq for DMA\n");
			return -EBUSY;
		}
	}

	return 0;
}

static void dma_exit(void)
{
	free_irq(irq_no, 1);
	iounmap(dma_regs);
	class_device_destroy(cls, MKDEV(major, 0));
	class_destroy(cls);
	cdev_del(&dma_cdev);
	unregister_chrdev_region(MKDEV(major, 0), 1);
	dma_free_writecombine(NULL, BUF_SIZE, src, src_phys);
	dma_free_writecombine(NULL, BUF_SIZE, dst, dst_phys);	
}

module_init(dma_init);
module_exit(dma_exit);
MODULE_LICENSE("GPL");

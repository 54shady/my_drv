/*  �ο�
 *	drivers\mtd\nand\at91_nand.c
 *	drivers\mtd\nand\s3c2410.c
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#include <asm/arch/regs-nand.h>
#include <asm/arch/nand.h>

struct s3c_nand_regs {
	unsigned long nfconf  ;
	unsigned long nfcont  ;
	unsigned long nfcmd   ;
	unsigned long nfaddr  ;
	unsigned long nfdata  ;
	unsigned long nfeccd0 ;
	unsigned long nfeccd1 ;
	unsigned long nfeccd  ;
	unsigned long nfstat  ;
	unsigned long nfestat0;
	unsigned long nfestat1;
	unsigned long nfmecc0 ;
	unsigned long nfmecc1 ;
	unsigned long nfsecc  ;
	unsigned long nfsblk  ;
	unsigned long nfeblk  ;
};

static struct mtd_partition s3c_nand_parts[] = {
	[0] = {
        .name   = "uboot",
        .size   = 0x00040000,
		.offset	= 0,
	},
	[1] = {
        .name   = "params",
        .offset = MTDPART_OFS_APPEND,
        .size   = 0x00020000,
	},
	[2] = {
        .name   = "kernel",
        .offset = MTDPART_OFS_APPEND,
        .size   = 0x00200000,
	},
	[3] = {
        .name   = "rootfs",
        .offset = MTDPART_OFS_APPEND,
        .size   = MTDPART_SIZ_FULL,
	}
};

static struct s3c_nand_regs *s3c_nand_regs;
static struct nand_chip *s3c_nand_chip;
static struct mtd_info *s3c_mtd_info;
#define S3C_MAX_CHIP 	1

static void s3c2440_select_chip(struct mtd_info *mtd, int chipnr)
{
	switch (chipnr) 
	{
		case -1:
			/* ȡ��Ƭѡ  NFCONT[1]=1 */
			s3c_nand_regs->nfcont |= (1<<1);
			break;

		case 0:
			/* ѡ��  NFCONT[1]=0 */
			s3c_nand_regs->nfcont &= ~(1<<1);
			break;

		default:
			break;
	}
}

static void s3c2440_cmd_ctrl(struct mtd_info *mtd, int cmd_or_addr, unsigned int ctrl)
{
	if (cmd_or_addr == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
	{
		/* ������ NFCMD = cmd_or_addr */
		s3c_nand_regs->nfcmd = cmd_or_addr;
	}
	else
	{
		/* ����ַ NFADDR = cmd_or_addr */
		s3c_nand_regs->nfaddr = cmd_or_addr;
	}
}
static int s3c2440_dev_ready(struct mtd_info *mtd)
{
	/* �ж� NFSTAT[0] */
	return (s3c_nand_regs->nfstat & (1<<0));
}


static int s3c_nand_init(void) 
{
	struct clk *s3c_nand_clk;
	
	/* 1. ����һ��nand_chip�ṹ�� */
	s3c_nand_chip = kzalloc(sizeof(struct nand_chip), GFP_KERNEL);

	s3c_nand_regs = ioremap(0x4E000000, sizeof(struct s3c_nand_regs));
	/* 2. ����nand_chip */
	/* �����nand_chip�Ǹ�nand_scan�õ�,���Կ����ȿ�nand_scan�����ʹ�õ� */

	s3c_nand_chip->select_chip = s3c2440_select_chip;
	s3c_nand_chip->cmd_ctrl    = s3c2440_cmd_ctrl;

	/* "NFDATA�������ַ"; ע������Ҫȡַ�� */
	s3c_nand_chip->IO_ADDR_R   = &s3c_nand_regs->nfdata;
	/* "NFDATA�������ַ"; */
	s3c_nand_chip->IO_ADDR_W   = &s3c_nand_regs->nfdata;
	
	s3c_nand_chip->dev_ready   = s3c2440_dev_ready;
	s3c_nand_chip->ecc.mode = NAND_ECC_SOFT;	/* enable ECC */
	
	/* 3. Ӳ����ص����� */
	/* ʹ��NAND FLASH��������ʱ�� */
	s3c_nand_clk = clk_get(NULL, "nand");
	/* ʵ���Ͼ�������CLKCON�Ĵ�����bit4 */
	clk_enable(s3c_nand_clk);
	
#define TACLS	0	
#define TWRPH0	1	
#define TWRPH1	0
	s3c_nand_regs->nfconf = (TWRPH1<<4) | (TWRPH0<<8) | (TACLS<<12);

	/*
	 * NFCONT
	 * BIT1=1,ȡ��Ƭѡ
	 * BIT0=1,ʹ��NAND FLASH������
	 */
	 s3c_nand_regs->nfcont = (1<<1) | (1<<0);
	
	/* 4. ʹ��nand_scan */
	s3c_mtd_info = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	s3c_mtd_info->priv = s3c_nand_chip;
	s3c_mtd_info->owner = THIS_MODULE;

	/* ɨ��ʶ��NAND FLASH */
	nand_scan(s3c_mtd_info, S3C_MAX_CHIP);

	/* 5. add_mtd_patitions */
	/* �����������Ļ���add_mtd_device�͹��� ��4���������� nbparts=4 */
	add_mtd_partitions(s3c_mtd_info, s3c_nand_parts, 4);
	return 0;
}

static void s3c_nand_exit(void)
{
	iounmap(s3c_nand_regs);
	del_mtd_partitions(s3c_mtd_info);
	kfree(s3c_nand_chip);
	kfree(s3c_mtd_info);
}

module_init(s3c_nand_init);
module_exit(s3c_nand_exit);
MODULE_LICENSE("GPL");


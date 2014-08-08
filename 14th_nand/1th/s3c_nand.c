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

static struct nand_chip *s3c_nand_chip;
static struct mtd_info *s3c_mtd_info;
#define S3C_MAX_CHIP 	1

static void s3c2440_select_chip(struct mtd_info *mtd, int chipnr)
{
	switch (chipnr) {
	case -1:
		/* ȡ��Ƭѡ  NFCONT[1]=0 */
		
		break;
		/* ѡ��  NFCONT[1]=1 */
	case 0:
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
	}
	else
	{
		/* ����ַ NFADDR = cmd_or_addr */
	}
}
static int s3c2440_dev_ready(struct mtd_info *mtd)
{
	/* �ж� NFSTAT */
}


static int s3c_nand_init(void) 
{
	/* 1. ����һ��nand_chip�ṹ�� */
	s3c_nand_chip = kzalloc(sizeof(struct nand_chip), GFP_KERNEL);

	/* 2. ����nand_chip */
	/* �����nand_chip�Ǹ�nand_scan�õ�,���Կ����ȿ�nand_scan�����ʹ�õ� */

	s3c_nand_chip->select_chip = s3c2440_select_chip;
	s3c_nand_chip->cmd_ctrl    = s3c2440_cmd_ctrl;
	s3c_nand_chip->IO_ADDR_R   = "NFDATA�������ַ";
	s3c_nand_chip->IO_ADDR_W   = "NFDATA�������ַ";
	s3c_nand_chip->dev_ready   = s3c2440_dev_ready;
	
	/* 3. Ӳ����ص����� */

	/* 4. ʹ��nand_scan */
	s3c_mtd_info = kzalloc(sizeof(struct mtd_info), GFP_KERNEL);
	s3c_mtd_info->priv = s3c_nand_chip;
	s3c_mtd_info->owner = THIS_MODULE;

	/* ɨ��ʶ��NAND FLASH */
	nand_scan(s3c_mtd_info, S3C_MAX_CHIP);
	return 0;
}

static void s3c_nand_exit(void)
{
	kfree(s3c_nand_chip);
	kfree(s3c_mtd_info);
}

module_init(s3c_nand_init);
module_exit(s3c_nand_exit);
MODULE_LICENSE("GPL");


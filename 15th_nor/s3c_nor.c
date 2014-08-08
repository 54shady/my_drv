
/*
*参考drivers\mtd\maps\Physmap.c
*/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <asm/io.h>

static struct map_info *s3c_nor_map;
static struct mtd_info *s3c_mtd_info;
static struct mtd_partition s3c_nor_part[] = {
	[0] = {
        .name   = "bootloader",
        .size   = 0x00040000,
		.offset	= 0,
	},
	[1] = {
        .name   = "root_nor",
        .offset = MTDPART_OFS_APPEND,
        .size   = MTDPART_SIZ_FULL,
	},
};

static int s3c_nor_init(void)
{
	/* 1. 分配map_info结构体 */
	s3c_nor_map = kzalloc(sizeof(struct map_info), GFP_KERNEL);

	/* 2. 设置：phyaddr,size,banksize,viraddr */
	s3c_nor_map->name = "s3c_nor";
	s3c_nor_map->phys = 0;/* 基地址 */
	s3c_nor_map->size = 0x1000000; /* >= NOR的真正大小 */
	s3c_nor_map->bankwidth = 2; /* 位宽 */
	s3c_nor_map->virt = ioremap(s3c_nor_map->phys, s3c_nor_map->size);
	simple_map_init(s3c_nor_map);

	/* 3. 使用：调用NOR FLASH协议层的函数来识别 */	
	printk("Use cfi_probe\n");
	s3c_mtd_info= do_map_probe("cfi_probe", s3c_nor_map);
	if (!s3c_mtd_info)
	{
		printk("Use jedec_probe\n");
		s3c_mtd_info= do_map_probe("jedec_probe", s3c_nor_map);
	}

	if (!s3c_mtd_info)
	{
		kfree(s3c_nor_map);
		iounmap(s3c_nor_map->virt);
		printk("can't probe the NOR FLASH\n");
		return -EIO;
	}
	/* 4. add_mtd_partitions */
	add_mtd_partitions(s3c_mtd_info, s3c_nor_part, 2);
	return 0;
}

static void s3c_nor_exit(void)
{
	kfree(s3c_nor_map);
	iounmap(s3c_nor_map->virt);
	del_mtd_partitions(s3c_mtd_info);
}

module_init(s3c_nor_init);
module_exit(s3c_nor_exit);

MODULE_LICENSE("GPL");


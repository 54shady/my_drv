/* 参考:
 * drivers\block\Xd.c
 * drivers\block\Z2ram.c
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/dma.h>

static struct gendisk *ramblock_gendisk;
static request_queue_t *ramblock_request_queue;
static int major;
#define RAMBLOCK_SIZE	(1024*1024)
static unsigned char *ramblock_buf;

static DEFINE_SPINLOCK(ramblock_lock);
static int ramblock_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	/* 容量=heads*cylinders*sectors*512 */
	/* 自定义有2个柱面 */
	geo->heads     = 2;

	/* 自定义有32个环面 */
	geo->cylinders = 32;

	/* 根据上面的数据来求出下面的值扇区*/
	geo->sectors   = RAMBLOCK_SIZE/2/32/512;
	return 0;
}

static struct block_device_operations ramblock_fops =
{
	.owner		= THIS_MODULE,
	.getgeo     = ramblock_getgeo,
};

static void do_ramblock_request(request_queue_t *q)
{
	struct request *req;

	while ((req = elv_next_request(q)) != NULL) 
	{
		/* 数据传输三要素:源，目的，长度 */
		/* 源/目的 */
		unsigned long offset = req->sector << 9;

		/* 目的/源 */
		/* req->buffer */
		
		/* 长度 */
		unsigned long len  = req->current_nr_sectors << 9;

		if (rq_data_dir(req) == READ)
		{
			memcpy(req->buffer, ramblock_buf+offset, len);
		}
		else
		{
			memcpy(ramblock_buf+offset, req->buffer, len);
		}

 		end_request(req, 1);
	}
	
}
static int ramblock_init(void)
{
    major = register_blkdev(0, "ramblock");
	
	/* 1.分配一个gendisk结构体 */
	/* 这里的16表示分区的个数 15个分区 */
    ramblock_gendisk = alloc_disk(16);
	
	/* 2. 设置 */
    ramblock_gendisk->major = major;
    ramblock_gendisk->first_minor = 0;
	sprintf(ramblock_gendisk->disk_name, "ramblock");
    ramblock_gendisk->fops = &ramblock_fops;

	/* 2.1 分配/设置队列:提供读写能力 */
    ramblock_request_queue = blk_init_queue(do_ramblock_request, &ramblock_lock);
    ramblock_gendisk->queue = ramblock_request_queue;
	
	/* 2.2 设置其它属性:比如容量 */
	set_capacity(ramblock_gendisk, RAMBLOCK_SIZE / 512);

	/* 3. 硬件相关操作 */
	if (NULL == (ramblock_buf = kzalloc(RAMBLOCK_SIZE, GFP_KERNEL)))
		return -ENOMEM;
	
	/* 4. 注册 */
    add_disk(ramblock_gendisk);
	
	return 0;
}

static void ramblock_exit(void)
{
	unregister_blkdev(major, "ramblock");
	del_gendisk(ramblock_gendisk);

	/*  put_disk(ramblock_gendisk);
	 *  视频里有这一句话，我这里注释掉貌似也没什么问题
	 *  应该是需要的，要把计数器减去1
	 */

	blk_cleanup_queue(ramblock_request_queue);
	kfree(ramblock_buf);
}

module_init(ramblock_init);
module_exit(ramblock_exit);
MODULE_LICENSE("GPL");

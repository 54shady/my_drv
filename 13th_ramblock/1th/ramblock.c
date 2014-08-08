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

static DEFINE_SPINLOCK(ramblock_lock);

static struct block_device_operations ramblock_fops =
{
	.owner		= THIS_MODULE,
};

static void do_ramblock_request(request_queue_t *q)
{
	struct request *req;
	static int cnt = 0;
	printk("do_ramblock_request %d\n", ++cnt);
	
	while ((req = elv_next_request(q)) != NULL) 
	{
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
	
	/* 3. 注册 */
    add_disk(ramblock_gendisk);
	
	return 0;
}

static void ramblock_exit(void)
{
	unregister_blkdev(major, "ramblock");
	del_gendisk(ramblock_gendisk);
	/* put_dist(ramblock_gendisk);*/
	blk_cleanup_queue(ramblock_request_queue);
}

module_init(ramblock_init);
module_exit(ramblock_exit);
MODULE_LICENSE("GPL");


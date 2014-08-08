/* �ο�:
 * drivers\net\Cs89x0.c
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

static struct net_device *virt_net_dev;
static int my_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	static int cnt = 0;
	printk("my_hard_start_xmit cnt = %d\n", ++cnt);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes = skb->len;
	
	return 0;
}

static int virt_net_init(void)
{
	/* 1. ����һ��net_dev�ṹ�� */
	/* virt_net_dev = alloc_etherdev(sizeof(struct net_device)); */
	virt_net_dev = alloc_netdev(0, "myVnet%d", ether_setup);
	if (!virt_net_dev)
		return -ENOMEM;

	/* 2. ���� */
	virt_net_dev->hard_start_xmit = my_hard_start_xmit;

    virt_net_dev->dev_addr[0] = 0x08;
    virt_net_dev->dev_addr[1] = 0x88;
    virt_net_dev->dev_addr[2] = 0x88;
    virt_net_dev->dev_addr[3] = 0x88;
    virt_net_dev->dev_addr[4] = 0x88;
    virt_net_dev->dev_addr[5] = 0x88;

	/* 3. ע�� */
	register_netdev(virt_net_dev);
	return 0;
}

static void virt_net_exit(void)
{
	unregister_netdev(virt_net_dev);
	free_netdev(virt_net_dev);
}

module_init(virt_net_init);
module_exit(virt_net_exit);
MODULE_AUTHOR("M_O_Bz163.com");
MODULE_LICENSE("GPL");


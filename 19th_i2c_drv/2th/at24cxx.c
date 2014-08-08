/*
 * �ο�:
 * drivers\i2c\chips\eeprom.c
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

static unsigned short ignore[] = { I2C_CLIENT_END };

/* ����оƬ�ֲ�AT24C08 P8 ��ַ����7λ 1010 A2 A1 A0 
 * ����A2 A1 A0 ��ԭ��ͼ������λ�ӵ����Զ���0
 */
static unsigned short normal_addr[] = { 0x50, I2C_CLIENT_END };
/* ��0x50��Ϊ0x60�����ڲ������豸��ַΪ0x60���豸
 * ���Ժ���at24cxx_detect���ᱻ����
 * �������õ�ַΪ0x60��ʱ��Ҳȥ����at24cxx_detect����
 */

static unsigned short forces_addr[] = {ANY_I2C_BUS, 0x60, I2C_CLIENT_END};
static unsigned short *at24cxx_forces[] = {forces_addr, NULL};

static struct i2c_client_address_data addr_data = {
	.normal_i2c	= ignore,
	.probe		= ignore,
	.ignore		= ignore,
	/* ǿ����Ϊ����at24cxx_forces�е��豸��ַ 
	 * �����ڵ�I2C�豸��û�н���ʱ����ϵͳ�����н���
	 */
	.forces		= at24cxx_forces,
};

static int at24cxx_detect(struct i2c_adapter *adapter, int address, int kind)
{
	printk("at24cxx detect is being called\n");
	
	return 0;
}
static int at24cxx_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, at24cxx_detect);
}

/* 1.����һ��i2c_driver �ṹ�� */
static struct i2c_driver at24cxx_driver = {
	.driver = {
		.name	= "at24cxx",
	},
	.attach_adapter	= at24cxx_attach_adapter,
};

static int at24cxx_init(void)
{
	/* 2.ע�� i2c_driver */
	return i2c_add_driver(&at24cxx_driver);
}

static void at24cxx_exit(void)
{
	i2c_del_driver(&at24cxx_driver);
}

module_init(at24cxx_init);
module_exit(at24cxx_exit);
MODULE_LICENSE("GPL");

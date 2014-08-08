/*
 * 参考:
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

/* 根据芯片手册AT24C08 P8 地址长度7位 1010 A2 A1 A0 
 * 其中A2 A1 A0 看原理图，这三位接地所以都是0
 */
static unsigned short normal_addr[] = { 0x50, I2C_CLIENT_END };

static struct i2c_client_address_data addr_data = {
	.normal_i2c	= normal_addr,
	.probe		= ignore,
	.ignore		= ignore,
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

/* 1.分配一个i2c_driver 结构体 */
static struct i2c_driver at24cxx_driver = {
	.driver = {
		.name	= "at24cxx",
	},
	.attach_adapter	= at24cxx_attach_adapter,
};

static int at24cxx_init(void)
{
	/* 2.注册 i2c_driver */
	return i2c_add_driver(&at24cxx_driver);
}

static void at24cxx_exit(void)
{
	i2c_del_driver(&at24cxx_driver);
}

module_init(at24cxx_init);
module_exit(at24cxx_exit);
MODULE_LICENSE("GPL");

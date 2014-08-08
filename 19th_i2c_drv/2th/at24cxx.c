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
/* 把0x50改为0x60后，由于不存在设备地址为0x60的设备
 * 所以函数at24cxx_detect不会被调用
 * 现在想让地址为0x60的时候也去调用at24cxx_detect函数
 */

static unsigned short forces_addr[] = {ANY_I2C_BUS, 0x60, I2C_CLIENT_END};
static unsigned short *at24cxx_forces[] = {forces_addr, NULL};

static struct i2c_client_address_data addr_data = {
	.normal_i2c	= ignore,
	.probe		= ignore,
	.ignore		= ignore,
	/* 强制认为存在at24cxx_forces中的设备地址 
	 * 适用于当I2C设备还没有接入时，在系统运行中接入
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

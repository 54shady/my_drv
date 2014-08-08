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
#include <linux/fs.h>
#include <asm/uaccess.h>

#define I2C_NAME_SIZE 20

static unsigned short ignore[] = { I2C_CLIENT_END };
static struct class *cls;
static 	struct i2c_client *at24cxx_client;

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
	.normal_i2c	= normal_addr,
	.probe		= ignore,
	.ignore		= ignore,
	/* 强制认为存在at24cxx_forces中的设备地址 
	 * 适用于当I2C设备还没有接入时，在系统运行中接入
	 */
//	.forces		= at24cxx_forces,
};

static int at24cxx_attach_adapter(struct i2c_adapter *adapter);
static int at24cxx_detach_client(struct i2c_client *client);

/* 1.分配一个i2c_driver 结构体 */
static struct i2c_driver at24cxx_driver = {
	.driver = {
		.name	= "at24cxx",
	},
	.attach_adapter	= at24cxx_attach_adapter,
	.detach_client	= at24cxx_detach_client,
};

static ssize_t at24cxx_write(struct file *file, const char __user *buf, size_t size, loff_t *offset);

static ssize_t at24cxx_read(struct file * file, char __user * buf, size_t size, loff_t * offset)
{
	unsigned char address;
	unsigned char data;
	struct i2c_msg msg[2];
	int ret;
	
	if (size != 1)
		return -EINVAL;

	if (copy_from_user(&address, buf, 1))
		return -1;
	

	/* 数据传输三要素:源，目的，长度 */
	
	/* 读AT24CXX时，要先把要读的存储空间的地址发给I2C设备 */
	msg[0].addr = at24cxx_client->addr; /* 目的 */
	msg[0].buf = &address; /* 源 */
	msg[0].len = 1; /* 长度 */
	msg[0].flags = 0;/* 0 表示写 */

	/* 然后启动读操作 */
	msg[1].addr = at24cxx_client->addr; /* 源 */
	msg[1].buf = &data; /* 目的 */
	msg[1].len = 1; /* 长度 */
	msg[1].flags = I2C_M_RD;/* 0 表示读 */
	
	if ((ret = i2c_transfer(at24cxx_client->adapter, msg, 2)) != 2)
		return -EIO;
	else
	{
		if (copy_to_user(buf, &data, 1))
			return -1;
		
		return ret;
	}

}

static ssize_t at24cxx_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
	/* address = buf[0]
	 * data    = buf[1]
	 */
	unsigned char val[2];
	struct i2c_msg msg[1];
	int ret;
	
	if (size != 2)
		return -EINVAL;

	if (copy_from_user(val, buf, 2))
		return -1;

	/* 数据传输三要素:源，目的，长度 */
	msg[0].addr = at24cxx_client->addr; /* 目的 */
	msg[0].buf = val; /* 源 */
	msg[0].len = 2; /* 长度 */
	msg[0].flags = 0;/* 0 表示写 */
	
	if ((ret = i2c_transfer(at24cxx_client->adapter, msg, 1)) != 1)
		return -EIO;
	else
		return ret;
}

static int major;
static struct file_operations at24cxx_ops = {
	.owner	= THIS_MODULE,
	.read	= at24cxx_read,
	.write  = at24cxx_write,
};


static int at24cxx_detect(struct i2c_adapter *adapter, int address, int kind)
{
	printk("at24cxx detect is being called\n");

	/* 构造一个i2c_client结构体,以后收发数据就用这个结构体 */
	/* 之前卸载驱动的时候没有调用at24cxx_detach_client函数就是因为没有构造
	 * i2c_client这个结构体
	 */

	if (!(at24cxx_client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL)))
		return -ENOMEM;

	at24cxx_client->addr = address;
	at24cxx_client->adapter = adapter;
	at24cxx_client->driver = &at24cxx_driver;
	//at24cxx_client->flags = 0;

	/* Fill in the remaining client fields */
	strlcpy(at24cxx_client->name, "at24cxx", I2C_NAME_SIZE);

	if (i2c_attach_client(at24cxx_client))
		return -1;

	if ((major = register_chrdev(0, "at24cxx", &at24cxx_ops)) < 0)
		printk("Failed to register character device.");

	cls = class_create(THIS_MODULE, "at24cxx");
	if (IS_ERR(cls))
		return -1;

	class_device_create(cls, NULL, MKDEV(major, 0), NULL, "at24cxx");/* /dev/at24cxx */
		
	return 0;
}

static int at24cxx_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, at24cxx_detect);
}

static int at24cxx_detach_client(struct i2c_client *client)
{
	int err;

	class_device_destroy(cls, MKDEV(major,0));
	class_destroy(cls);
	unregister_chrdev(major, "at24cxx");

	err = i2c_detach_client(client);
	if (err)
		return err;

	kfree(i2c_get_clientdata(client));

	return 0;
}

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

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
#include <linux/fs.h>
#include <asm/uaccess.h>

#define I2C_NAME_SIZE 20

static unsigned short ignore[] = { I2C_CLIENT_END };
static struct class *cls;
static 	struct i2c_client *at24cxx_client;

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
	.normal_i2c	= normal_addr,
	.probe		= ignore,
	.ignore		= ignore,
	/* ǿ����Ϊ����at24cxx_forces�е��豸��ַ 
	 * �����ڵ�I2C�豸��û�н���ʱ����ϵͳ�����н���
	 */
//	.forces		= at24cxx_forces,
};

static int at24cxx_attach_adapter(struct i2c_adapter *adapter);
static int at24cxx_detach_client(struct i2c_client *client);

/* 1.����һ��i2c_driver �ṹ�� */
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
	

	/* ���ݴ�����Ҫ��:Դ��Ŀ�ģ����� */
	
	/* ��AT24CXXʱ��Ҫ�Ȱ�Ҫ���Ĵ洢�ռ�ĵ�ַ����I2C�豸 */
	msg[0].addr = at24cxx_client->addr; /* Ŀ�� */
	msg[0].buf = &address; /* Դ */
	msg[0].len = 1; /* ���� */
	msg[0].flags = 0;/* 0 ��ʾд */

	/* Ȼ������������ */
	msg[1].addr = at24cxx_client->addr; /* Դ */
	msg[1].buf = &data; /* Ŀ�� */
	msg[1].len = 1; /* ���� */
	msg[1].flags = I2C_M_RD;/* 0 ��ʾ�� */
	
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

	/* ���ݴ�����Ҫ��:Դ��Ŀ�ģ����� */
	msg[0].addr = at24cxx_client->addr; /* Ŀ�� */
	msg[0].buf = val; /* Դ */
	msg[0].len = 2; /* ���� */
	msg[0].flags = 0;/* 0 ��ʾд */
	
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

	/* ����һ��i2c_client�ṹ��,�Ժ��շ����ݾ�������ṹ�� */
	/* ֮ǰж��������ʱ��û�е���at24cxx_detach_client����������Ϊû�й���
	 * i2c_client����ṹ��
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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define GPIOLED_CNT       1        /*设备号个数*/
#define GPIOLED_NAME  "gpioled"     /*设备名字*/
#define LEDOFF           0        /*关灯*/
#define LEDON            1        /*开灯*/

/*gpioled结构体*/
struct gpioled_dev{
    dev_t devid;            /*设备号*/
    struct cdev cdev;       /*cdev*/
    struct class *class;    /*lei*/
    struct device *device;  /*设备*/
    int major;              /*主设备号*/
    int minor;              /*次设备号*/
    struct device_node * nd; /*设备节点*/
    int led_gpio;            /*led所使用的gpio编号*/
};

struct gpioled_dev gpioled;    /*定义led设备*/

/*打开设备*/
static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &gpioled;           /* 设置私有数据 */
    return 0;
}

/*从设备读取数据*/
static ssize_t led_read(struct file *filp, char __user *buf,size_t cnt, loff_t *offt)
{
    return 0;
}

/*向设备写数据*/
static ssize_t led_write(struct file *filp,const char __user *buf,size_t cnt,loff_t *offt)
{
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;
    struct gpioled_dev *dev = filp->private_data;

    retvalue = copy_from_user(databuf, buf, cnt);
    if(retvalue < 0) {
    printk("kernel write failed!\r\n");
    return -EFAULT;
    }

    ledstat = databuf[0]; /* 获取状态值 */

    if(ledstat == LEDON) {
        gpio_set_value(dev->led_gpio,0);
    } else if(ledstat == LEDOFF) {
        gpio_set_value(dev->led_gpio,1); /* 关闭 LED 灯 */
    }
    return 0;
}

/*关闭/释放设备*/
static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* 设备操作函数 */
static struct file_operations gpioled_fops={
    .owner=THIS_MODULE,
    .open=led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

/*模块入口和出口*/
static int __init gpioled_init(void)
{
    int ret=0;

/*设置LED所使用的GPIO*/
    /*1、获取设备节点：gpioled*/
    gpioled.nd=of_find_node_by_path("/gpioled");
    if(gpioled.nd == NULL){
        printk("gpioled node can not found!\r\n");
        return  -EFAULT;
    }else{
        printk("gpioled node has been found!\r\n");
    }

    /*2、获取设备树中的gpio属性，得到LED所使用的LED编号*/
    gpioled.led_gpio=of_get_named_gpio(gpioled.nd,"led-gpio",0);
    if(gpioled.led_gpio<0){
        printk("can't get led-gpio");
        return -EFAULT;
    }
    printk("led-gpio num= %d\r\n",gpioled.led_gpio);

    /*3、申请IO*/
    ret = gpio_request(gpioled.led_gpio,"led-gpio");
    if(ret){
        printk("Failed to request the led-gpio\r\n");
        return -EFAULT;
    }

    /* 4、设置GPIO_IO03为输出，并且输出高电平，默认关闭LED */
    ret = gpio_direction_output(gpioled.led_gpio,1);
    if(ret < 0) {
        printk("can't set gpio!\r\n");
    }

/*注册字符设备驱动*/
    /*1、注册设备号*/
    if(gpioled.major){            /*定义设备号*/
    gpioled.devid=MKDEV(gpioled.major,0);
    register_chrdev_region(gpioled.devid,GPIOLED_CNT,GPIOLED_NAME);
    }else{                         /*没有定义设备*/
        alloc_chrdev_region(&gpioled.devid,0,GPIOLED_CNT,GPIOLED_NAME);  /*申请设备号*/
        gpioled.major=MAJOR(gpioled.devid);                            /* 获取分配号的主设备号 */
        gpioled.minor=MINOR(gpioled.devid);
    }
    printk("gpioled major=%d, minor=%d\r\n",gpioled.major,gpioled.minor);

    /*2、初始化*/
    gpioled.cdev.owner=THIS_MODULE;
    cdev_init(&gpioled.cdev,&gpioled_fops);

    /*3、添加一个cdev*/
    cdev_add(&gpioled.cdev,gpioled.devid,GPIOLED_CNT);

    /*4、创建类*/
    gpioled.class = class_create(THIS_MODULE, GPIOLED_NAME);
    if (IS_ERR(gpioled.class)) {
        return PTR_ERR(gpioled.class);
    }

    /*创建设备*/
    gpioled.device = device_create(gpioled.class, NULL, gpioled.devid,NULL, GPIOLED_NAME);
    if (IS_ERR(gpioled.device)) {
        return PTR_ERR(gpioled.device);
    }

    return 0;
}


/*驱动出口*/
static void __exit gpioled_exit(void)
{
      
    /* 注销字符设备驱动 */
    cdev_del(&gpioled.cdev);                             /* 删除 cdev */
    unregister_chrdev_region(gpioled.devid, GPIOLED_CNT); /*注销设备号*/

    device_destroy(gpioled.class, gpioled.devid);
    class_destroy(gpioled.class);

    /*释放IO*/
    gpio_free(gpioled.led_gpio);

}

/*注册模块入口和出口*/
module_init(gpioled_init);
module_exit(gpioled_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("bruce");
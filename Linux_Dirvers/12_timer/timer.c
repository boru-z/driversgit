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
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/irq.h>


#define TIMER_CNT           1          /*设备号个数*/
#define TIMER_NAME       "timer"       /*设备名字*/
#define CLOSE_CMD     (_IO(0XEF,0x1))  /*关闭定时器*/
#define OPEN_CMD      (_IO(0XEF,0x2))  /*打开定时器*/
#define SETPERIOD_CMD (_IO(0XEF,0x3))  /*设置定时器周期命令*/
#define LEDOFF           0             /*关灯*/
#define LEDON            1             /*开灯*/

/*timer设备结构体*/
struct timer_dev{
    dev_t devid;            /*设备号*/
    struct cdev cdev;       /*cdev*/
    struct class *class;    /*lei*/
    struct device *device;  /*设备*/
    int major;              /*主设备号*/
    int minor;              /*次设备号*/
    struct device_node * nd; /*设备节点*/
    int led_gpio;            /*led所使用的gpio编号*/
    int timerperiod;          /*定时周期，单位为ms*/
    struct timer_list timer; /*定义一个定时器*/
    spinlock_t lock;           /*定义自旋锁*/
};

struct timer_dev timerdev;    /*定义timer设备*/

/*初始化 LED 灯 IO， open 函数打开驱动的时候初始化 LED 灯所使用的 GPIO 引脚。*/
static int led_init(void)
{
    int ret=0;

    /*1、获取设备节点：gpioled*/
    timerdev.nd=of_find_node_by_path("/gpioled");
    if(timerdev.nd == NULL){
        printk("gpioled node can not found!\r\n");
        return  -EFAULT;
    }else{
        printk("gpioled node has been found!\r\n");
    }

    /*2、获取设备树中的gpio属性，得到LED所使用的LED编号*/
    timerdev.led_gpio=of_get_named_gpio(timerdev.nd,"led-gpio",0);
    if(timerdev.led_gpio<0){
        printk("can't get led-gpio");
        return -EFAULT;
    }
    printk("led-gpio num= %d\r\n",timerdev.led_gpio);

    /*3、申请IO*/
    ret = gpio_request(timerdev.led_gpio,"led");
    if(ret){
        printk("Failed to request the led\r\n");
        return -EFAULT;
    }

    /* 4、设置GPIO_IO03为输出，并且输出高电平，默认关闭LED */
    ret = gpio_direction_output(timerdev.led_gpio,1);
    if(ret < 0) {
        printk("can't set gpio!\r\n");
    }
    return 0;
}

/*打开设备*/
static int timer_open(struct inode *inode, struct file *filp)
{
    int ret = 0;
    filp->private_data = &timerdev;           /* 设置私有数据 */

    timerdev.timerperiod = 1000;               /*默认周期1s*/ 
    ret=led_init();                             /*初始化LED IO*/
    if(ret<0){
        return ret;
    }
    return 0;
}

/*ioctl 函数，
filp : 要打开的设备文件(文件描述符)
cmd : 应用程序发送过来的命令
arg : 参数
return : 0 成功;其他 失败*/
static long timer_unlocked_ioctl(struct file *filp,unsigned int cmd,unsigned long arg)
{
    struct timer_dev *dev=(struct timer_dev*)filp->private_data;
    int timerperiod;
    unsigned long flags;

    switch(cmd){
        case CLOSE_CMD:   /*关闭定时器*/
            del_timer_sync(&dev->timer);
            break;

        case OPEN_CMD:      /*打开定时器*/
            spin_lock_irqsave(&dev->lock,flags);
            timerperiod=dev->timerperiod;
            spin_unlock_irqrestore(&dev->lock,flags);
            mod_timer(&dev->timer,jiffies+msecs_to_jiffies(timerperiod));
            break;

        case SETPERIOD_CMD:       /*设置定时器周期*/
            spin_lock_irqsave(&dev->lock,flags);
            dev->timerperiod=arg;
            spin_unlock_irqrestore(&dev->lock,flags);
            mod_timer(&dev->timer,jiffies+msecs_to_jiffies(arg));
            break;
        default:
            break;
    }
    return 0;
}

/* 设备操作函数 */
static struct file_operations timer_fops={
    .owner=THIS_MODULE,
    .open=timer_open,
    .unlocked_ioctl=timer_unlocked_ioctl,
};

/*定时器回调处理函数*/
void timer_function(unsigned long arg)
{
    struct timer_dev *dev = (struct timer_dev *)arg;
    static int sta=1;
    int  timerperiod;

    unsigned long flags;

    sta = !sta; /* 每次都取反，实现 LED 灯反转 */
    gpio_set_value(dev->led_gpio,sta);

    /*重启定时器*/
    spin_lock_irqsave(&dev->lock,flags);
    timerperiod=dev->timerperiod;
    spin_unlock_irqrestore(&dev->lock,flags);
    mod_timer(&dev->timer,jiffies+msecs_to_jiffies(dev->timerperiod));
}

/*模块入口函数*/
static int __init timer_init(void)
{
    /*初始化自旋锁*/
    spin_lock_init(&timerdev.lock);
 
     /*注册字符设备驱动*/
    /*1、注册设备号*/
    if(timerdev.major){            /*定义设备号*/
    timerdev.devid=MKDEV(timerdev.major,0);
    register_chrdev_region(timerdev.devid,TIMER_CNT,TIMER_NAME);
    }else{                         /*没有定义设备*/
        alloc_chrdev_region(&timerdev.devid,0,TIMER_CNT,TIMER_NAME);  /*申请设备号*/
        timerdev.major=MAJOR(timerdev.devid);                            /* 获取分配号的主设备号 */
        timerdev.minor=MINOR(timerdev.devid);
    }
    printk("timerdev major=%d, minor=%d\r\n",timerdev.major,timerdev.minor);

    /*2、初始化cdev*/
    timerdev.cdev.owner=THIS_MODULE;
    cdev_init(&timerdev.cdev,&timer_fops);

    /*3、添加一个cdev*/
    cdev_add(&timerdev.cdev,timerdev.devid,TIMER_CNT);

    /*4、创建类*/
    timerdev.class = class_create(THIS_MODULE, TIMER_NAME);
    if (IS_ERR(timerdev.class)) {
        return PTR_ERR(timerdev.class);
    }

    /*5、创建设备*/
    timerdev.device = device_create(timerdev.class, NULL, timerdev.devid,NULL, TIMER_NAME);
    if (IS_ERR(timerdev.device)) {
        return PTR_ERR(timerdev.device);
    }
    /*6、初始化timer，设置定时器处理函数，还未设置周期，所以不会激活定时器*/
    init_timer(&timerdev.timer);
    timerdev.timer.function=timer_function;
    timerdev.timer.data=(unsigned long)&timerdev;
    return 0;
}


/*驱动出口*/
static void __exit timer_exit(void)
{
     gpio_set_value(timerdev.led_gpio,1);  /* 卸载驱动的时候关闭 LED */
     del_timer_sync(&timerdev.timer);        /*删除timer*/
    
    /* 注销字符设备驱动 */
    cdev_del(&timerdev.cdev);                             /* 删除 cdev */
    unregister_chrdev_region(timerdev.devid, TIMER_CNT); /*注销设备号*/

    device_destroy(timerdev.class, timerdev.devid);
    class_destroy(timerdev.class);

    /*释放IO*/
    gpio_free(timerdev.led_gpio);

}

/*注册模块入口和出口*/
module_init(timer_init);
module_exit(timer_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("bruce");
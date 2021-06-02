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
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define IMX6UIRQ_CNT           1          /*设备号个数*/
#define IMX6UIRQ_NAME       "blockio"       /*设备名字*/
#define KEY0VALUE              0x01        /*KEY0按键值*/
#define INVAKEY                0xEF        /*无效按键值*/
#define KEY_NUM                 1            /*按键数量*/


/*中断IO描述结构体*/
struct irq_keydesc{
    int gpio; /* gpio */
    int irqnum; /* 中断号 */
    unsigned char value; /* 按键对应的键值 */
    char name[10]; /* 名字 */
    irqreturn_t (*handler)(int, void *); /* 中断服务函数 */
};

/*imx6uirq设备结构体*/
struct imx6uirq_dev{
    dev_t devid;            /*设备号*/
    struct cdev cdev;       /*cdev*/
    struct class *class;    /*lei*/
    struct device *device;  /*设备*/
    int major;              /*主设备号*/
    int minor;              /*次设备号*/
    struct device_node * nd; /*设备节点*/
    atomic_t keyvalue;       /*有效按键值*/
    atomic_t releasekey;     /*标记是否完成一次完整的按键*/
    struct timer_list timer; /*定义一个定时器*/
    struct irq_keydesc irqkey[KEY_NUM]; /*按键描述数组*/
    unsigned char curkeynum;            /*当前按键号*/
    wait_queue_head_t r_wait;          /*读等待队列头*/
};

struct imx6uirq_dev imx6uirq;    /*定义irq设备*/

/*中断服务函数，开启定时器，延时10ms,定时器用于按键消抖*/
static irqreturn_t key0_handler(int irq, void *dev_id)
{
    struct imx6uirq_dev *dev = (struct imx6uirq_dev *)dev_id;

    dev->curkeynum = 0;
    dev->timer.data=(volatile long)dev_id;
    mod_timer(&dev->timer,jiffies+msecs_to_jiffies(10));

    return IRQ_RETVAL(IRQ_HANDLED);
}

/*定时器服务函数，用于按键消抖，定时器到了以后
再次读取按键值，如果按键还是处于按下状态就表示按键有效。*/
 void timer_function(unsigned long arg)
 {
     unsigned char value;
     unsigned char num;
     struct irq_keydesc *keydesc;
     struct imx6uirq_dev *dev=(struct imx6uirq_dev*)arg;

     num=dev->curkeynum;
    keydesc=&dev->irqkey[num];
    value=gpio_get_value(keydesc->gpio);/* 读取 IO 值 */
    if(value == 0){                   /* 按下按键 */
        atomic_set(&dev->keyvalue,keydesc->value);
    }else{
        atomic_set(&dev->keyvalue, 0x80|keydesc->value);/* 按键松开 */
        atomic_set(&dev->releasekey, 1); /* 标记松开按键 */
    }

    /*唤醒进程*/
    if(atomic_read(&dev->releasekey)){            /* 完成一次按键过程 */
        wake_up_interruptible(&dev->r_wait);
    }
 }

/*初始化按键 IO*/
static int keyio_init(struct imx6uirq_dev *dev)
{
    unsigned char i = 0;
    int ret=0;

    /*1、获取设备节点：key*/
    dev->nd=of_find_node_by_path("/key");
    if(dev->nd == NULL){
        printk("key node can not found!\r\n");
        return  -EFAULT;
    }else{
        printk("key node has been found!\r\n");
    }

    /*2、获取设备树中的gpio属性，得到key所使用的编号*/
    for(i=0;i<KEY_NUM;i++){
        dev->irqkey[i].gpio =of_get_named_gpio(dev->nd,"key-gpio",i);
        if(dev->irqkey[i].gpio<0){
            printk("can't get key%d\r\n",i);
            return -EFAULT;
        }
        printk("key%d：gpio= %d",i,dev->irqkey[i].gpio);
    }

    /*3. 初始化 key 所使用的 IO，并且设置成中断模式 */
    for(i=0;i<KEY_NUM;i++){
        memset(dev->irqkey[i].name,0,sizeof(dev->irqkey[i].name));
        sprintf(dev->irqkey[i].name,"KEY%d",i);
        gpio_request(dev->irqkey[i].gpio,dev->irqkey[i].name);
        gpio_direction_input(dev->irqkey[i].gpio);
        dev->irqkey[i].irqnum=gpio_to_irq(dev->irqkey[i].gpio);

        printk(" irqnum=%d\r\n",dev->irqkey[i].irqnum);
    }
    
    /*4. 申请中断 */
    dev->irqkey[0].handler=key0_handler;
    dev->irqkey[0].value=KEY0VALUE;

    for(i=0;i<KEY_NUM;i++){
        ret=request_irq(dev->irqkey[i].irqnum,dev->irqkey[i].handler,IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING,dev->irqkey[i].name,&imx6uirq);
        if(ret<0){
            printk("irq %d request failed!\r\n",dev->irqkey[i].irqnum);
            return -EFAULT;
        }
        printk("irq %d request done!\r\n",dev->irqkey[i].irqnum);
    }

    /*5. 创建定时器*/
    init_timer(&dev->timer);
    dev->timer.function=timer_function;

    /*6.初始化等待队列头*/
    init_waitqueue_head(&dev->r_wait);

    return 0;
}

/*打开设备*/
static int imx6uirq_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &imx6uirq;           /* 设置私有数据 */
    return 0;
}

/*从设备读取数据*/
static ssize_t imx6uirq_read(struct file *filp, char __user *buf,size_t cnt, loff_t *offt)
{
    int ret = 0;
    unsigned char keyvalue=0;
    unsigned char releasekey=0;
    struct imx6uirq_dev *dev = (struct imx6uirq_dev *)filp->private_data;

    DECLARE_WAITQUEUE(wait,current);   /* 定义一个等待队列 */
    if(atomic_read(&dev->releasekey) == 0) { /* 没有按键按下 */
        add_wait_queue(&dev->r_wait, &wait); /* 添加到等待队列头 */
        __set_current_state(TASK_INTERRUPTIBLE);/* 设置任务状态 */
        schedule(); /* 进行一次任务切换 */
        if(signal_pending(current)) { /* 判断是否为信号引起的唤醒 */
            ret = -ERESTARTSYS;
            goto wait_error;
        }
        __set_current_state(TASK_RUNNING); /*设置为运行状态 */
        remove_wait_queue(&dev->r_wait, &wait); /*将等待队列移除 */
    }

    keyvalue=atomic_read(&dev->keyvalue);
    releasekey=atomic_read(&dev->releasekey);
    if(releasekey){             /*有按键按下*/
        if(keyvalue & 0x80){
            keyvalue &= ~0x80;
            ret=copy_to_user(buf,&keyvalue,sizeof(keyvalue));
        }else{
            goto data_error;
        }
        atomic_set(&dev->releasekey,0);   /* 按下标志清零 */
    }else{
        goto data_error;
    }
    return 0;

wait_error:
    set_current_state(TASK_RUNNING); /* 设置任务为运行态 */
    remove_wait_queue(&dev->r_wait, &wait); /* 将等待队列移除 */
    return ret;

data_error:
    return -EINVAL;
}

/* 设备操作函数 */
static struct file_operations imx6uirq_fops={
    .owner=THIS_MODULE,
    .open=imx6uirq_open,
    .read=imx6uirq_read,
};

/*驱动模块入口函数*/
static int __init imx6uirq_init(void)
{
     /*注册字符设备驱动*/
    /*1、注册设备号*/
    if(imx6uirq.major){            /*定义设备号*/
    imx6uirq.devid=MKDEV(imx6uirq.major,0);
    register_chrdev_region(imx6uirq.devid,IMX6UIRQ_CNT,IMX6UIRQ_NAME);
    }else{                         /*没有定义设备*/
        alloc_chrdev_region(&imx6uirq.devid,0,IMX6UIRQ_CNT,IMX6UIRQ_NAME);  /*申请设备号*/
        imx6uirq.major=MAJOR(imx6uirq.devid);                            /* 获取分配号的主设备号 */
        imx6uirq.minor=MINOR(imx6uirq.devid);
    }
    printk("imx6uirq major=%d, minor=%d\r\n",imx6uirq.major,imx6uirq.minor);

    /*2、初始化cdev*/
    //imx6uirq.cdev.owner=THIS_MODULE;
    cdev_init(&imx6uirq.cdev,&imx6uirq_fops);

    /*3、添加一个cdev*/
    cdev_add(&imx6uirq.cdev,imx6uirq.devid,IMX6UIRQ_CNT);

    /*4、创建类*/
    imx6uirq.class = class_create(THIS_MODULE, IMX6UIRQ_NAME);
    if (IS_ERR(imx6uirq.class)) {
        return PTR_ERR(imx6uirq.class);
    }

    /*5、创建设备*/
    imx6uirq.device = device_create(imx6uirq.class, NULL, imx6uirq.devid,NULL, IMX6UIRQ_NAME);
    if (IS_ERR(imx6uirq.device)) {
        return PTR_ERR(imx6uirq.device);
    }

    /*6、初始化按键*/
    atomic_set(&imx6uirq.keyvalue,INVAKEY);
    atomic_set(&imx6uirq.releasekey,0);
    keyio_init(&imx6uirq);
    return 0;
}


/*驱动出口*/
static void __exit imx6uirq_exit(void)
{
     unsigned int i = 0;

     del_timer_sync(&imx6uirq.timer);        /*删除timer定时器*/

    /*释放中断*/
    for(i=0;i<KEY_NUM;i++){
        free_irq(imx6uirq.irqkey[i].irqnum,&imx6uirq);
    }
    
    /* 注销字符设备驱动 */
    cdev_del(&imx6uirq.cdev);                             /* 删除 cdev */
    unregister_chrdev_region(imx6uirq.devid, IMX6UIRQ_CNT); /*注销设备号*/

    device_destroy(imx6uirq.class, imx6uirq.devid);
    class_destroy(imx6uirq.class);

    /*释放IO*/
   // for(i=0;i<KEY_NUM;i++)
    //gpio_free(imx6uirq.irqkey[i].gpio);

}

/*注册模块入口和出口*/
module_init(imx6uirq_init);
module_exit(imx6uirq_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("bruce");
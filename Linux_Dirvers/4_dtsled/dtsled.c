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
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define DTSLED_CNT       1        /*设备号个数*/
#define DTSLED_NAME  "dtsled"     /*设备名字*/
#define LEDOFF           0        /*关灯*/
#define LEDON            1        /*开灯*/

/* 映射后的寄存器虚拟地址指针 */
static void __iomem *IMX6U_CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;


/*dtsled结构体*/
struct dtsled_dev{
    dev_t devid;            /*设备号*/
    struct cdev cdev;       /*cdev*/
    struct class *class;    /*lei*/
    struct device *device;  /*设备*/
    int major;              /*主设备号*/
    int minor;              /*次设备号*/
    struct device_node * nd; /*设备节点*/
};

struct dtsled_dev dtsled;    /*定义led设备*/

/*LED 打开/关闭*/
void led_switch(u8 sta)
{
    u32 val = 0;
    if(sta == LEDON) {
        val = readl(GPIO1_DR);
        val &= ~(1 << 3);
        writel(val, GPIO1_DR);
    }else if(sta == LEDOFF) {
        val = readl(GPIO1_DR);
        val|= (1 << 3);
        writel(val, GPIO1_DR);
    }
}

/*打开设备*/
static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &dtsled;           /* 设置私有数据 */
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

    retvalue = copy_from_user(databuf, buf, cnt);
    if(retvalue < 0) {
    printk("kernel write failed!\r\n");
    return -EFAULT;
    }

    ledstat = databuf[0]; /* 获取状态值 */

    if(ledstat == LEDON) {
        led_switch(LEDON); /* 打开 LED 灯 */
    } else if(ledstat == LEDOFF) {
            led_switch(LEDOFF); /* 关闭 LED 灯 */
    }
    return 0;
}

/*关闭/释放设备*/
static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* 设备操作函数 */
static struct file_operations dtsled_fops={
    .owner=THIS_MODULE,
    .open=led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

/*模块入口和出口*/
static int __init dtsled_init(void)
{
    int ret ;
    u32 val = 0;
    u32 regdata[14];
    const char *str;
    struct property *proper;

/*获取设备树中的属性数据*/
    /*1\获取设备节点：alphaled*/
    dtsled.nd=of_find_node_by_path("/alphaled");
    if(dtsled.nd == NULL){
        printk("alphaled node can not found!\r\n");
        return  -EFAULT;
    }else{
        printk("alphaled node has been found!\r\n");
    }

    /*2、获取compatible属性内容*/
    proper=of_find_property(dtsled.nd,"compatible",NULL);
    if(proper==NULL){
        printk("compatible property find falide!\r\n");
    }else{
        printk("compatible = %s\r\n", (char*)proper->value);
    }

    /*3、获取status属性*/
    ret = of_property_read_string(dtsled.nd, "status", &str);
    if(ret < 0){
        printk("status read failed!\r\n");
    } else {
        printk("status = %s\r\n",str);
    }

    /* 4、获取 reg 属性内容 */
    ret = of_property_read_u32_array(dtsled.nd, "reg", regdata, 10);
    if(ret < 0) {
        printk("reg property read failed!\r\n");
    } else {
        u8 i = 0;
        printk("reg data:\r\n");
        for(i = 0; i < 10; i++)
            printk("%#X ", regdata[i]);
        printk("\r\n");
    }

    /* 初始化 LED */
    /* 1、寄存器地址映射 */
#if 0
    IMX6U_CCM_CCGR1 = ioremap(regdata[0], regdata[1]);
    SW_MUX_GPIO1_IO03 = ioremap(regdata[2], regdata[3]);
    SW_PAD_GPIO1_IO03 = ioremap(regdata[4], regdata[5]);
    GPIO1_DR = ioremap(regdata[6], regdata[7]);
    GPIO1_GDIR = ioremap(regdata[8], regdata[9]);
#else
    IMX6U_CCM_CCGR1 = of_iomap(dtsled.nd, 0);
    SW_MUX_GPIO1_IO03 = of_iomap(dtsled.nd, 1);
    SW_PAD_GPIO1_IO03 = of_iomap(dtsled.nd, 2);
    GPIO1_DR = of_iomap(dtsled.nd, 3);
    GPIO1_GDIR = of_iomap(dtsled.nd, 4);
#endif

    /* 2、使能 GPIO1 时钟 */
    val = readl(IMX6U_CCM_CCGR1);
    val &= ~(3 << 26);              /* 清楚以前的设置 */
    val |= (3 << 26);               /* 设置新值 */
    writel(val, IMX6U_CCM_CCGR1);

    /* 3、设置 GPIO1_IO03 的复用功能，将其复用为GPIO1_IO03，最后设置 IO 属性。*/
    writel(5, SW_MUX_GPIO1_IO03);

        /* 寄存器 SW_PAD_GPIO1_IO03 设置 IO 属性 */
    writel(0x10B0, SW_PAD_GPIO1_IO03);  

    /* 4、设置 GPIO1_IO03 为输出功能 */
    val = readl(GPIO1_GDIR);
    val &= ~(1 << 3);                 /* 清除以前的设置 */
    val |= (1 << 3);                  /* 设置为输出 */
    writel(val, GPIO1_GDIR);

    /* 5、默认关闭 LED */
    val = readl(GPIO1_DR);
    val |= (1 << 3);
    writel(val, GPIO1_DR);

/*注册字符设备驱动*/
    /*1、注册设备号*/
    if(dtsled.major){            /*定义设备号*/
    dtsled.devid=MKDEV(dtsled.major,0);
    register_chrdev_region(dtsled.devid,DTSLED_CNT,DTSLED_NAME);
    }else{                         /*没有定义设备*/
        alloc_chrdev_region(&dtsled.devid,0,DTSLED_CNT,DTSLED_NAME);  /*申请设备号*/
        dtsled.major=MAJOR(dtsled.devid);                            /* 获取分配号的主设备号 */
        dtsled.minor=MINOR(dtsled.devid);
    }
    printk("dtsled major=%d,minor=%d\r\n",dtsled.major,dtsled.minor);

    /*2、初始化*/
    dtsled.cdev.owner=THIS_MODULE;
    cdev_init(&dtsled.cdev,&dtsled_fops);

    /*3、添加一个cdev*/
    cdev_add(&dtsled.cdev,dtsled.devid,DTSLED_CNT);

    /*4、创建类*/
    dtsled.class = class_create(THIS_MODULE, DTSLED_NAME);
    if (IS_ERR(dtsled.class)) {
        return PTR_ERR(dtsled.class);
    }

    /*创建设备*/
    dtsled.device = device_create(dtsled.class, NULL, dtsled.devid,NULL, DTSLED_NAME);
    if (IS_ERR(dtsled.device)) {
        return PTR_ERR(dtsled.device);
    }

    return 0;
}


/*驱动出口*/
static void __exit dtsled_exit(void)
{
    /*取消映射*/
    iounmap(IMX6U_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_DR);
    iounmap(GPIO1_GDIR);
      
    /* 注销字符设备驱动 */
    cdev_del(&dtsled.cdev);                             /* 删除 cdev */
    unregister_chrdev_region(dtsled.devid, DTSLED_CNT); /*注销设备号*/

    device_destroy(dtsled.class, dtsled.devid);
    class_destroy(dtsled.class);

}

/*注册模块入口和出口*/
module_init(dtsled_init);
module_exit(dtsled_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("bruce");
#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "linux/ioctl.h"

/* 使用方法 ：./irqApp /dev/imx6uirq 打开测试 App*/

int main(int argc, char* argv[])
{
    int fd;
    int ret = 0;
    char * filename;
    unsigned char data;

    if(argc!=2){
        printf("Error Usage!\r\n");
        return -1;
    }

    filename = argv[1];

    /*打开驱动*/
    fd=open(filename,O_RDWR);
    if(fd<0){
        printf("file %s open failed!\r\n",argv[1]);
        return -1;
    }

    /*/dev/irq读取数据*/
    while(1){
        ret=read(fd,&data,sizeof(data));
        if(ret<0){

        }else{
            if(data){
                printf("key value = %#X\r\n",data);
            }   
        }    
    }
    close(fd);/*关闭文件*/

    return ret;
}
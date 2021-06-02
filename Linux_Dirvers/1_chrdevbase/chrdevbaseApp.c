#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

static char usrdata[]={"usr data!"};

/*
* @description : main 主程序
* @param - argc : argv 数组元素个数
* @param - argv : 具体参数
* @return : 0 成功;其他 失败
*/
int main(int argc, char *argv[])
{
    int fd,retvalue;
    char *filename;
    char readbuf[100],writebuf[100];

    if(argc != 3)
    {
        printf("Error Usage!\r\n");
        return -1;
    }

    filename=argv[1];

    /*打开驱动文件*/
    fd=open(filename,O_RDWR);
    if(fd<0)
    {
        printf("Can't open file %s\r\n",filename);
        return -1;

    }

    /* 从驱动文件读取数据 */
    if(atoi(argv[2])==1)
    {
        retvalue=read(fd,readbuf,50);
        if(retvalue<0)
        {
            printf("read file %s failed!\r\n",filename);
        }else
        {
            printf("read data: %s\r\n",readbuf);
        }
    }

    /* 向设备驱动写数据 */
    if(atoi(argv[2])==2)
    {
        memcpy(writebuf,usrdata,sizeof(usrdata));
        retvalue=write(fd,writebuf,50);
        if(retvalue<0)
        {
            printf("write file %s failed!\r\n",filename);
        }   
    }

    /*关闭设备*/
    retvalue=close(fd);
    if(retvalue<0)
    {
        printf("Can't close file %s\r\n",filename);
        return -1;
    }

    return 0;
}
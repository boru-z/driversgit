#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "linux/ioctl.h"
#include "poll.h"
#include "sys/time.h"
#include "sys/select.h"

/* 使用方法 ：./blockioApp /dev/blockio 打开测试 App*/

int main(int argc, char* argv[])
{
    int fd;
    int ret = 0;
    char * filename;
    unsigned char data;
    struct pollfd fds;
    fd_set readfds;
    struct timeval timeout;

    if(argc!=2){
        printf("Error Usage!\r\n");
        return -1;
    }

    filename = argv[1];

    /*打开驱动*/
    fd=open(filename,O_RDWR | O_NONBLOCK);    /*非阻塞访问*/
    if(fd<0){
        printf("file %s open failed!\r\n",argv[1]);
        return -1;
    }

    /*poll函数实现非阻塞访问*/
    fds.fd=fd;
    fds.events=POLLIN;

    while(1){
        ret=poll(&fds,1,500);
        if(ret){              /* 数据有效 */       
            ret=read(fd,&data,sizeof(data));
            if(data)
                printf("key value = %#X\r\n",data);     
        }else if(ret==0){                /* 超时 */
           // printf("timeout!\r\n");
        }else if(ret <0){
            //printf("error!\r\n");         /* 错误 */
        }   
    }

#if 0
    /*select函数实现*/
    while(1){
        FD_ZERO(&readfds);
        FD_SET(fd,&readfds);
        /*构造超时时间*/
        timeout.tv_sec=0;
        timeout.tv_usec=500000;/*500ms*/
        ret =select(fd+1,&readfds,NULL,NULL,&timeout);

        switch(ret){
            case 0：
                printf("timeout!\r\n"); 
                break;
            case -1:
                printf("error!\r\n");         /* 错误 */
                break;
            default:
                if(FD_ISSET(fd,&readfds)){
                    ret=read(fd,&data,sizeof(data));
                    if(ret<0){

                    }else{
                        if(data)
                        printf("key value= %d\r\n",data);
                    }
                }
                break;
        }
    }
#endif

    close(fd);/*关闭文件*/
    return ret;
}
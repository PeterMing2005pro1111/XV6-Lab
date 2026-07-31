#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
//先引入用户态经常用的这三个头文件
int main(int argc,char *argv[])
{
    if(argc<2)//没有真正的参数的情况
    {
        fprintf(2,"缺少输入的参数\n");
        exit(1);
    }
    else
    {
        int time=atoi(argv[1]);//time就是要睡眠的时长
        sleep(time);
    }
    exit(0);
}
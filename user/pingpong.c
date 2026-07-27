#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
int main(int argc,char *argv[])
{
    int pid;
    char buffer[20]={0};//设置一个缓冲区buffer
    int parent[2];
    int child[2];
    pipe(parent);
    pipe(child);//创建两个管道

    if((pid=fork())==0)//是0，说明是子进程,它的功能是回复pong
    {
        close(parent[1]);//先关闭父进程的写端
        read(parent[0],buffer,4);//从父进程的读端读数据,读给缓冲区
        printf("%d: received %s\n",getpid(),buffer);//从缓冲区中把读到的数据给打印出来
        close(child[0]);
        write(child[1],"pong",4);//把pong写入子进程的写端
        exit(0);
    }
    else
    {
        close(parent[0]);
        write(parent[1],"ping",4);
        close(child[1]);
        read(child[0],buffer,sizeof(buffer));
        printf("%d: received %s\n",getpid(),buffer);//读到的东西通过缓冲区来进行打印，很安全
        exit(0);
    }
}
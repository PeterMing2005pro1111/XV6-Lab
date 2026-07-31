#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void send_primes(int pd[], int infos[], int infoslen)
{
    int info;
    close(pd[0]);
    for(int i = 0; i < infoslen; i++) {
        info = infos[i];
        write(pd[1], &info, sizeof(info));
    }
    close(pd[1]);
}

void process(int pd[])
{
    int p;
    int n;
    int pid;
    int child[2];
    int infos[34];
    int infos_i = 0;
    
    close(pd[1]);  // 关闭写端
    
    // 读取第一个素数
    if(read(pd[0], &p, sizeof(p)) != sizeof(p)) {
        close(pd[0]);
        exit(0);
    }
    printf("prime %d\n", p);
    
    // 读取剩余数字
    while(read(pd[0], &n, sizeof(n)) == sizeof(n)) {
        if(n % p != 0) {
            infos[infos_i++] = n;
        }
    }
    close(pd[0]);
    
    if(infos_i == 0) {
        exit(0);
    }
    
    pipe(child);
    
    if((pid = fork()) == 0) {
        // 子进程：关闭所有不必要的文件描述符
        close(child[1]);  // 子进程只读，关闭写端
        process(child);
        exit(0);
    } else {
        // 父进程：关闭读端，发送数据
        close(child[0]);
        for(int i = 0; i < infos_i; i++) {
            write(child[1], &infos[i], sizeof(int));
        }
        close(child[1]);  // 关键：关闭写端通知子进程
        wait(0);  // 等待子进程
        exit(0);
    }
}

void generate_nums(int nums[34])
{
    for(int i = 0; i < 34; i++) {
        nums[i] = i + 2;
    }
}

int main(int argc, char** argv)
{
    int pd[2];
    int pid;
    int nums[34];
    
    pipe(pd);
    generate_nums(nums);
    
    if((pid = fork()) == 0) {
        // 子进程
        close(pd[1]);  // 关闭写端
        process(pd);
        exit(0);
    } else {
        // 父进程
        close(pd[0]);  // 关闭读端
        for(int i = 0; i < 34; i++) {
            write(pd[1], &nums[i], sizeof(int));
        }
        close(pd[1]);  // 关闭写端
        wait(0);  // 等待子进程
        exit(0);
    }
}
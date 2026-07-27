#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(2, "Usage: xargs <command> [args...]\n");
        exit(1);
    }

    // 保存传递给子进程的参数数组
    char *new_argv[MAXARG];
    int base_argc = 0;

    // 将 xargs 接收到的基础命令及初始参数存入 new_argv
    // argv[0] 是 "xargs"，所以从 i = 1 开始拷贝
    for (int i = 1; i < argc; i++) {
        new_argv[base_argc++] = argv[i];
    }

    char buf[512]; // 用于存放从标准输入读取的一行文本
    int p = 0;     // 缓冲区索引

    // 从标准输入 (fd = 0) 逐个字符读取数据
    while (read(0, &buf[p], 1) == 1) {
        if (buf[p] == '\n') {
            buf[p] = '\0'; // 将'\n'替换为C字符串终止符'\0'
            // 将从 stdin 读取的这一行作为追加参数拼接到参数列表中
            new_argv[base_argc] = buf;
            new_argv[base_argc + 1] = 0; //exec要求参数数组末尾必须为NULL(0)

            //fork 子进程并执行命令
            if (fork() == 0) {
                // 子进程：执行目标命令
                exec(new_argv[0], new_argv);
                // 如果 exec 成功，不会返回；若返回则说明执行失败
                fprintf(2, "xargs: exec %s failed\n", new_argv[0]);
                exit(1);
            } else {
                // 父进程：等待子进程完成，避免产生僵尸进程
                wait(0);
            }

            p = 0; // 重置缓冲区指针，准备读取下一行
        } else {
            p++; // 普通字符，继续写入缓冲区
        }
    }

    exit(0);
}
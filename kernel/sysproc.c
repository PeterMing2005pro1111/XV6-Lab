#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;
  backtrace();

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
uint64
sys_sigalarm(void)
{
  int interval;
  uint64 handler;

  // 获取两个参数：interval和handler 指针
  if(argint(0, &interval) < 0 || argaddr(1, &handler) < 0)
    return -1;

  struct proc *p = myproc();
  p->alarm_interval = interval;
  p->alarm_handler = (void(*)())handler;
  p->alarm_ticks = 0;

  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();

  // 1. 备份当前 trapframe 中的内核信息（非常关键！）
  uint64 ksatp = p->trapframe->kernel_satp;
  uint64 ksp = p->trapframe->kernel_sp;
  uint64 ktrap = p->trapframe->kernel_trap;
  uint64 khartid = p->trapframe->kernel_hartid;

  // 2. 恢复被打断时的全部用户上下文
  *p->trapframe = p->alarm_tf;

  // 3. 把内核信息放回 trapframe，防止 trampoline.S 下一次陷入内核时崩溃
  p->trapframe->kernel_satp = ksatp;
  p->trapframe->kernel_sp = ksp;
  p->trapframe->kernel_trap = ktrap;
  p->trapframe->kernel_hartid = khartid;

  // 4. 重置 alarm 锁和计数器
  p->alarm_goingoff = 0;
  p->alarm_ticks = 0;

  // 5. 返回保存的 a0 寄存器值，避免破坏系统调用/中断发生时的 a0
  return p->trapframe->a0;
}
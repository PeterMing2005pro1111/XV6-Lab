#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
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


#ifdef LAB_PGTBL

#endif

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
int
sys_pgaccess(void)
{
  uint64 base_va; // 待检查的首个页面的虚拟地址
  int len;        // 需要检查的页面数量
  uint64 user_dst; // 用于接收 bitmask 的用户空间缓冲区地址

  argaddr(0, &base_va);
  argint(1, &len);
  argaddr(2, &user_dst);
  if (len < 0 || len > 64)
    return -1;

  struct proc *p = myproc();
  uint64 mask = 0; //存储按位记录的访问状态
  //遍历检查每一个页面
  for (int i = 0; i < len; i++) {
    uint64 va = base_va + i * PGSIZE;
    
    // 使用walk查找该虚拟地址对应的PTE（第三个参数传0，表示不分配新页表）
    pte_t *pte = walk(p->pagetable, va, 0);

    // 检查PTE是否有效且存在访问标志PTE_A
    if (pte != 0 && (*pte & PTE_V) && (*pte & PTE_A)) {
      mask |= (1L << i);  // 在掩码对应的第 i 位置 1
      *pte &= ~PTE_A;     // 必须清除 PTE_A 标志位,防止影响下次检测
    }
  }

  //将生成的64位bitmask写入用户指定的地址
  if (copyout(p->pagetable, user_dst, (char *)&mask, sizeof(mask)) < 0)
    return -1;

  return 0;
}
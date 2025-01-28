#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "pstat.h"

extern int global_tickets;
extern int global_stride;
extern struct {
    struct spinlock lock;
    struct proc proc[NPROC];
} ptable;

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
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

int
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

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_settickets(void)
{
  int n;
  if(argint(0, &n) < 0) // 从用户空间获取指针参数
    return -1;
  if(n < 1) // tickets返回
    n = DEFAULT_TICKETS;
  else if(n > MAX_TICKETS)
    n = MAX_TICKETS;

  struct proc *curproc = myproc();
  
  // 更新全局票数
  acquire(&ptable.lock);
  global_tickets -= curproc->tickets;
  curproc->tickets = n;
  global_tickets += curproc->tickets;
  global_stride = STRIDE1 / global_tickets;
  // 更新进程的 stride
  curproc->stride = STRIDE1 / curproc->tickets;
  release(&ptable.lock);

  return 0;
}

int
sys_getpinfo(void)
{
  struct pstat *ps;
  if(argptr(0, (void*)&ps, sizeof(*ps)) < 0) // 从用户空间获取指针参数
    return -1;

  struct proc *p;
  int i = 0;

  acquire(&ptable.lock);
  // 为ptable里面proc赋值各个属性
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++, i++){
    ps->inuse[i] = (p->state != UNUSED);
    ps->tickets[i] = p->tickets;
    ps->pid[i] = p->pid;
    ps->pass[i] = p->pass;
    ps->remain[i] = p->remain;
    ps->stride[i] = p->stride;
    ps->rtime[i] = p->rtime;
  }
  release(&ptable.lock);

  return 0;
}

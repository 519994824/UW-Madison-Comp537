#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "file.h"
#include "vm.h"

extern struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

extern char ref_count[];

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
// int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);

uint ticks;


void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_PGFLT: {
    uint fault_addr = rcr2(); // Get the address causing the fault
    struct proc *p = myproc();

    // Walk the page directory to locate the PTE
    pte_t *pte = walkpgdir(p->pgdir, (void *)fault_addr, 0);

    // Handle Copy-on-Write (COW) faults
    if (pte && (*pte & PTE_COW)) {
        uint pa = PTE_ADDR(*pte);
        char *new_page = kalloc();
        if (!new_page) {
            cprintf("Out of memory during COW fault handling\n");
            p->killed = 1;
            break;
        }

        // Copy the contents of the old page to the new page
        memmove(new_page, (char *)P2V(pa), PGSIZE);

        // Update reference count for the old page
          uint idx = pa / PGSIZE;
          if(kmem.use_lock)
            acquire(&kmem.lock);
          ref_count[idx]--;
          if(kmem.use_lock)
            release(&kmem.lock);

        // Update the PTE to point to the new page
        *pte = V2P(new_page) | PTE_W | PTE_U | PTE_P;

        // Flush the TLB
        lcr3(V2P(p->pgdir));
        break;
    }

    // Handle Lazy Allocation
    struct wmap_region *region = 0;
    for (int i = 0; i < MAX_WMAPS; i++) {
        uint start = p->wmap_regions[i].addr;
        uint end = start + p->wmap_regions[i].length;
        if (p->wmap_regions[i].length > 0 && fault_addr >= start && fault_addr < end) {
            region = &p->wmap_regions[i];
            break;
        }
    }

    if (region) {
        uint a = PGROUNDDOWN(fault_addr);
        char *mem = kalloc();
        if (!mem) {
            cprintf("Lazy allocation failed\n");
            p->killed = 1;
            break;
        }
        memset(mem, 0, PGSIZE);

        // Handle file-backed mapping
        if (region->f) {
          int file_offset = a - region->addr;
          ilock(region->f->ip);
          int n = readi(region->f->ip, mem, file_offset, PGSIZE);
          iunlock(region->f->ip);
          if (n < 0) {
              cprintf("File read error at 0x%p\n", a);
              kfree(mem);
              p->killed = 1;
              break;
          }
        }

        // Map the page
        if (mappages(p->pgdir, (char *)a, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0) {
            kfree(mem);
            cprintf("Mapping failed at 0x%p\n", a);
            p->killed = 1;
            break;
        }
        break;
    }

    // Segmentation Fault for invalid accesses
    cprintf("Segmentation Fault at 0x%p\n", fault_addr);
    p->killed = 1;
    break;
  }

  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}



#include "vm.h"

extern struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

extern char ref_count[];
extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// 分配一个新的 wmap_region 结构
struct wmap_region*
alloc_wmap_region(struct proc *p)
{
    if(p->wmap_count >= MAX_WMAPS)
        return 0; // 已达到最大映射数量

    for(int i = 0; i < MAX_WMAPS; i++) {
        if(p->wmap_regions[i].addr == 0) { // addr 为 0 表示未使用
            p->wmap_count++;
            return &p->wmap_regions[i];
        }
    }
    return 0; // 未找到可用的映射结构
}

// 从进程的映射列表中移除以 addr 开始的映射区域
struct wmap_region*
remove_wmap_region(struct proc *p, uint addr)
{
    for(int i = 0; i < MAX_WMAPS; i++) {
        if(p->wmap_regions[i].addr == addr) {
            struct wmap_region *region = &p->wmap_regions[i];
            // 不要立即将 region->addr 设置为 0
            p->wmap_count--;
            return region;
        }
    }
    return 0; // 未找到匹配的映射区域
}


int
is_region_free(struct proc *p, uint addr, int length)
{
    // 检查地址是否在有效范围内（0x60000000 到 0x80000000）
    if(addr < 0x60000000 || addr + length > 0x80000000)
        return 0;

    // 检查是否与进程的代码段和数据段重叠
    if(addr < p->sz && addr + length > 0)
        return 0; // 重叠，区域不可用

    // 检查是否与已有的内存映射重叠
    for(int i = 0; i < MAX_WMAPS; i++) {
        if(p->wmap_regions[i].addr != 0) {
            uint m_start = p->wmap_regions[i].addr;
            uint m_end = m_start + p->wmap_regions[i].length;
            if(!(addr + length <= m_start || addr >= m_end))
                return 0; // 重叠，区域不可用
        }
    }

    return 1; // 区域可用
}
int
do_wmap(uint addr, int length, int flags, int fd)
{
    struct proc *curproc = myproc();

     // Validate flags
    if (!(flags & MAP_FIXED)) {  // MAP_FIXED must be set
        cprintf("wmap error: MAP_FIXED flag not set\n");
        return FAILED;
    }
    if (!(flags & MAP_SHARED)) {  // MAP_SHARED must be set
        cprintf("wmap error: MAP_SHARED flag not set\n");
        return FAILED;
    }

    // 检查映射区域是否可用
    if(!is_region_free(curproc, addr, length))
        return FAILED;

    // 分配新的映射区域结构
    struct wmap_region *region = alloc_wmap_region(curproc);
    if(!region)
        return FAILED;

    // 初始化映射区域
    region->addr = addr;
    region->length = length;
    region->flags = flags;

    // 如果是文件映射，获取文件指针
    if(!(flags & MAP_ANONYMOUS)) {
        if(fd < 0 || fd >= NOFILE) // 文件描述符不能超过当前进程允许的最大打开文件数。
            return FAILED;
        struct file *f = curproc->ofile[fd];
        if(f == 0 || f->type != FD_INODE) //文件必须是普通文件（FD_INODE 表示 inode 文件类型）。
            return FAILED;
        region->f = filedup(f); // 增加文件引用计数
    } else {
        region->f = 0;
    }

    // 懒分配，不立即分配物理页
    return addr;

}

int
do_wunmap(uint addr)
{
    struct proc *curproc = myproc();
    struct wmap_region *region = remove_wmap_region(curproc, addr);

    if(!region)
        return FAILED;

    // 遍历映射区域的所有页，解除映射并释放物理页
    uint a;
    for(a = region->addr; a < region->addr + region->length; a += PGSIZE) {
        pte_t *pte = walkpgdir(curproc->pgdir, (void*)a, 0);
        if(pte && (*pte & PTE_P)) {
            // 如果是文件映射且带有 MAP_SHARED，写回文件
            if(region->f && (region->flags & MAP_SHARED)) {
                char *v = P2V(PTE_ADDR(*pte));
                int offset = a - region->addr;
                ilock(region->f->ip);
                begin_op();
                int n = writei(region->f->ip, v, offset, PGSIZE);
                end_op();
                iunlock(region->f->ip);
                if(n < 0) {
                    cprintf("Failed to write to file\n");
                    curproc->killed = 1;
                    return FAILED;
                }
            }

            // 释放物理页
            uint pa = PTE_ADDR(*pte);
            char *v = P2V(pa);
            kfree(v);
            *pte = 0;
        }
    }

    // 如果是文件映射，关闭文件
    if(region->f) {
        fileclose(region->f);
        region->f = 0;
    }

    // 现在将 region 标记为未使用
    region->addr = 0;
    region->length = 0;
    region->flags = 0;
    region->f = 0;

    return SUCCESS;
}

int do_va2pa(uint va) {
    struct proc *p = myproc(); // 当前进程
    pte_t *pte;

    // 检查虚拟地址是否对齐
    if (va >= KERNBASE || va % PGSIZE != 0) {
        return FAILED; // 无效地址
    }

    // 通过页表查找 PTE
    pte = walkpgdir(p->pgdir, (void *)va, 0);
    if (!pte || !(*pte & PTE_P)) {
        return FAILED; // 地址未映射
    }

    // 返回物理地址
    return PTE_ADDR(*pte) | (va & 0xFFF); // 页物理地址 + 偏移
}

int do_getwmapinfo(struct wmapinfo *info) {
    // struct proc *p = myproc(); // 当前进程
    // int i;

    // // 遍历 wmap 区域
    // for (i = 0; i < MAX_WMAPS; i++) {
    //     struct wmap_region *region = &p->wmap_regions[i];
    //     if (region->addr != 0) {
    //         cprintf("Region %d: addr=%p, length=%d, flags=%d\n",
    //                 i, region->addr, region->length, region->flags);
    //     }
    // }
    // return SUCCESS; // 成功

    struct proc *p = myproc(); // Current process
    int i, total_mmaps = 0;

    // Reset the info structure
    info->total_mmaps = 0;
    for (i = 0; i < MAX_WMAPS; i++) {
        info->addr[i] = 0;
        info->length[i] = 0;
        info->n_loaded_pages[i] = 0;
    }

    // Traverse wmap regions
    for (i = 0; i < MAX_WMAPS; i++) {
        struct wmap_region *region = &p->wmap_regions[i];
        if (region->length > 0) { // Valid mapping
            info->addr[total_mmaps] = region->addr;
            info->length[total_mmaps] = region->length;

            // Calculate the number of loaded pages
            int loaded_pages = 0;
            uint a;
            for (a = region->addr; a < region->addr + region->length; a += PGSIZE) {
                pte_t *pte = walkpgdir(p->pgdir, (void *)a, 0);
                if (pte && (*pte & PTE_P)) {
                    loaded_pages++;
                }
            }

            info->n_loaded_pages[total_mmaps] = loaded_pages;
            total_mmaps++;
        }
    }

    info->total_mmaps = total_mmaps; // Set total mappings
    return SUCCESS;
}

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
// loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz, uint flags)
// {
//   uint i, pa, n;
//   pte_t *pte;

//   if((uint) addr % PGSIZE != 0)
//     panic("loaduvm: addr must be page aligned");
//   for(i = 0; i < sz; i += PGSIZE){
//     if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
//       panic("loaduvm: address should exist");
//     pa = PTE_ADDR(*pte);
//     if(sz - i < PGSIZE)
//       n = sz - i;
//     else
//       n = PGSIZE;
//     if(readi(ip, P2V(pa), offset+i, n) != n)
//       return -1;
//   }
//   return 0;
// }
{
  uint i, pa, n;
  pte_t *pte;

  if((uint)addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr + i, 0)) == 0)
      panic("loaduvm: address should exist");
    if(!(*pte & PTE_P))
      panic("loaduvm: page not present");
    pa = PTE_ADDR(*pte);
    n = PGSIZE;
    if(sz - i < PGSIZE)
      n = sz - i;

    // 根据 flags 设置页表的权限
    int perm = PTE_U; // 默认用户态权限
    if(flags & ELF_PROG_FLAG_WRITE)
      perm |= PTE_W;  // 如果有写标志，则添加写权限

    // 将物理内存中的数据加载到该页
    if(readi(ip, (char*)P2V(pa), offset + i, n) != n)
      return -1;

    // 更新页表权限
    *pte = (*pte & ~0xFFF) | perm | PTE_P;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  // char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  // for(i = 0; i < sz; i += PGSIZE){
  for(i = 0; i < KERNBASE; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      // panic("copyuvm: pte should exist");
      continue;
    if(!(*pte & PTE_P))
      // panic("copyuvm: page not present");
      continue;

    // Get the physical address and flags for the page
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);

    // Increment reference count for the shared page
    uint page_idx = pa / PGSIZE;
    acquire(&kmem.lock);
    ref_count[page_idx]++;
    release(&kmem.lock);


    // Mark parent page as read-only and set the PTE_COW flag
    if (*pte & PTE_W) {
      *pte &= ~PTE_W;
      *pte |= PTE_COW;
    }

    // Flush the TLB for the parent
    lcr3(V2P(pgdir));

    // Map the page into the child's page table as read-only and COW
    if (mappages(d, (void *)i, PGSIZE, pa, (flags & ~PTE_W) | PTE_COW) < 0) {
      freevm(d);
      return 0;
    }


    // if((mem = kalloc()) == 0)
    //   goto bad;
    // memmove(mem, (char*)P2V(pa), PGSIZE);
    // if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
    //   kfree(mem);
    //   goto bad;
    // }
  }
  return d;

// bad:
//   freevm(d);
//   return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.


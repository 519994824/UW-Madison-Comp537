// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
// 在 kalloc.c 顶部
#define MAX_PHYS_PAGES (PHYSTOP / PGSIZE)
char ref_count[MAX_PHYS_PAGES];
void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE){
    kfree(p);
  }

}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// void
// kfree(char *v)
// {
//   struct run *r;

//   if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
//     panic("kfree");

//   // Fill with junk to catch dangling refs.
//   memset(v, 1, PGSIZE);

//   if(kmem.use_lock)
//     acquire(&kmem.lock);
//   r = (struct run*)v;
//   r->next = kmem.freelist;
//   kmem.freelist = r;
//   if(kmem.use_lock)
//     release(&kmem.lock);
// }

void
kfree(char *v)
{
  struct run *r;
  uint pa;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  pa = V2P(v);
  uint idx = pa / PGSIZE;

  if(kmem.use_lock)
    acquire(&kmem.lock);

  if(ref_count[idx] < 0)
    panic("kfree: ref_count underflow");

  if(ref_count[idx] == 0){
    // 初始化过程中，引用计数为 0，直接将页面加入空闲列表
    // 填充垃圾数据，捕获悬空引用
    memset(v, 1, PGSIZE);

    // 将页面加入空闲列表
    r = (struct run*)v;
    r->next = kmem.freelist;
    kmem.freelist = r;
  } else {
    ref_count[idx]--;
    if (ref_count[idx] == 0) {
      // 填充垃圾数据，捕获悬空引用
      memset(v, 1, PGSIZE);

      // 将页面加入空闲列表
      r = (struct run*)v;
      r->next = kmem.freelist;
      kmem.freelist = r;
    }
  }

  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    uint pa = V2P((char*)r);
    uint idx = pa / PGSIZE;
    ref_count[idx] = 1;
  }
  if(kmem.use_lock)
    release(&kmem.lock);
  if(r)
    memset((char*)r, 0, PGSIZE); // 清零页面内容
  return (char*)r;
}


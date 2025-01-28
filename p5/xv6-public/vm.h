#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

pte_t* walkpgdir(pde_t *pgdir, const void *va, int alloc);
int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm);
int do_wmap(uint addr, int length, int flags, int fd);
int do_wunmap(uint addr);
int do_va2pa(uint va);
int do_getwmapinfo(struct wmapinfo *wminfo);
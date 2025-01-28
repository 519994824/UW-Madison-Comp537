// kalloc.h

#ifndef KALLOC_H
#define KALLOC_H

void  kinit1(void *vstart, void *vend);
void  kinit2(void *vstart, void *vend);
char* kalloc(void);
void  kfree(char*);

#endif // KALLOC_H
#include "types.h"
#include "user.h"

int
main(void)
{
    char *ptr = sbrk(0);  // 获取当前的程序断点（堆的顶部）
    sbrk(4096);           // 分配一页内存
    ptr[0] = 'A';         // 触发页面的分配

    uint va = (uint)ptr;
    uint pa = va2pa(va);

    if(pa == (uint)-1){
        printf(1, "va2pa failed\n");
    } else {
        printf(1, "VA: 0x%x => PA: 0x%x\n", va, pa);
    }

    exit();
}
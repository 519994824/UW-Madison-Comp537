// wmaptest.c
#include "types.h"
#include "stat.h"
#include "user.h"
#include "wmap.h"
 
int main() {
    uint addr = 0x60000000;
    int length = 4096 * 2; // Two pages
    int flags = MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS;
 
    uint result = wmap(addr, length, flags, -1);
    if (result == (uint)-1 || result == 0) {
        printf(1, "wmap failed\n");
        exit();
    }
 
    // Write to mapped memory
    int *p = (int*)addr;
    p[0] = 123;
    p[1024] = 456;
 
    // Read back
    printf(1, "p[0] = %d\n", p[0]);
    printf(1, "p[1024] = %d\n", p[1024]);
    


    // Unmap
    if (wunmap(addr) == -1) {
        printf(1, "wunmap failed\n");
        exit();
    }
 
    exit();
}

#include "types.h"
#include "stat.h"
#include "user.h"

int main() {
    char *mem = sbrk(4096);  // 分配一页
    mem[0] = 'A';            // 写入数据

    int pid = fork();
    if (pid == 0) {
        // 子进程：尝试写入共享页
        mem[0] = 'B';
        printf(1, "Child: mem[0] = %c\n", mem[0]);
        exit();
    } else {
        wait();
        printf(1, "Parent: mem[0] = %c\n", mem[0]);
    }
    exit();
}
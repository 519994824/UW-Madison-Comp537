#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    size_t size_now = sizeof(int);
    printf("size of int: %zu\n", size_now);
    void* ptr = malloc(8);
    int* int_ptr = (int*) ptr;
    *int_ptr = 10;
    if (ptr != NULL)
    {
        printf("address: %p\n", (void*)&ptr);
        printf("address: %p\n", (void*)ptr);
        printf("address: %p\n", (void*)&int_ptr);
        printf("address: %p\n", (void*)int_ptr);
        printf("value: %d\n", *int_ptr);
    }
    return 0;
}
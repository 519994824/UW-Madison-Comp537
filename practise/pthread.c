#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

void *thread_function(void *arg) {
    int *result = malloc(sizeof(int));
    *result = 123;
    return result;
}

int main() {
    pthread_t thread;
    void *retval;
    pthread_create(&thread, NULL, thread_function, NULL);
    pthread_join(thread, &retval);

    // Access the result
    int *result = (int *)retval;
    printf("Thread returned: %d\n", *result);
    free(result);

    return 0;
}
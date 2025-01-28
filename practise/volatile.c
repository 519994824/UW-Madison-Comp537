#include <stdio.h>
#include <signal.h>
#include <unistd.h>

int counter = 0;  // Not declared as volatile

void handle_signal(int sig) {
    counter++;  // This function modifies the counter
}

int main() {
    signal(SIGALRM, handle_signal);  // Set up signal handler
    alarm(1);  // Set up an alarm to send a SIGALRM signal every second

    while (1) {
        printf("Counter: %d\n", counter);  // Print the counter value
        sleep(1);  // Wait for a second
    }

    return 0;
}
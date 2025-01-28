#include <stdio.h>

int main(int argc, char *argv[]) {
    // Print all command-line arguments
    for (int i = 0; i < argc; i++) {
        printf("Argument %d: %s\n", i, argv[i]);  // Print the entire string
    }

    // Print first character of the second argument (if it exists)
    if (argc > 1) {
        printf("First character of second argument: %c\n", *argv[1]);  // Print first character of argv[1]
    }

    return 0;
}
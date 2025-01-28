#include "user.h" // xv6's user-space header

char *str = "You can't change a character!";

int main() {
    str[1] = 'O';              // Modify the string
    printf(1, "%s\n", str);    // Use xv6's printf with file descriptor
    exit();                    // Use exit() instead of return
}
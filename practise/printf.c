#include <stdio.h>

int main() {
    // char ch = 'A';
    // printf("Character: %c\n", ch);  // Prints: Character: A
    // char input;
    // printf("Enter a character: ");
    // scanf("%c", &input);           // Reads a single character input
    // printf("You entered: %c\n", input);


    // char str[] = "Hello, World!";
    // printf("String: %s\n", str);  // Prints: String: Hello, World!
    // char input2[50];
    // printf("Enter a string: ");
    // scanf("%s", input2);          // Reads a string input (up to whitespace)
    // printf("You entered: %s\n", input2);


    char str[] = "abc";    // `str` is an array of characters
    char *p = str;         // `p` points to the start of `str`
    printf("Initial string: %s\n", str);  // Output: "abc"
    printf("First character: %c\n", *p);  // Output: 'a'
    *p = 'd';  // Modify the first character
    printf("Modified string: %s\n", str); // Output: "dbc"
    printf("First character after modification: %c\n", *p); // Output: 'd'

    return 0;
}
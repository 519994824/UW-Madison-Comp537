#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define INIT_BUFFER 8

char* read_file_dynamically(const char* file_path)
{
    FILE* file = fopen(file_path, "r");
    if (file == NULL)
    {
        perror("Unable to open file");
        exit(1);
    }
    size_t size = INIT_BUFFER;
    size_t length = 0;
    char* buffer = malloc(size);
    if (buffer == NULL)
    {
        perror("Unable to allocate buffer");
        fclose(file);
        exit(1);
    }
    int ch;
    while ((ch = fgetc(file)) != EOF)
    {
        if (length >= size - 1)
        {
            size_t new_size = size * 2;
            char* new_buffer = realloc(buffer, new_size);
            if (new_buffer == NULL)
            {
                perror("Unable to reallocate buffer");
                fclose(file);
                free(buffer);
                exit(1);
            }
            buffer = new_buffer;
            size = new_size;
        }
        buffer[length++] = (char)ch;
    }
    buffer[length] = '\0';
    fclose(file);
    return buffer;
}

char* read_stdin(void)
{
    size_t size = INIT_BUFFER;
    size_t length = 0;
    char* buffer = malloc(size);
    if (buffer == NULL)
    {
        perror("Unable to allocate buffer");
        exit(1);
    }
    int ch;
    while ((ch = getchar()) != EOF)
    {
        if (length >= size - 1)
        {
            size_t new_size = size * 2;
            char* new_buffer = realloc(buffer, new_size);
            if (new_buffer == NULL)
            {
                perror("Unable to reallocate buffer");
                free(buffer);
                exit(1);
            }
            buffer = new_buffer;
            size = new_size;
        }
        buffer[length++] = (char)ch;
    }
    buffer[length] = '\0';
    return buffer;
}


int* check_board(char* buffer)
{
    int* alpha_set = malloc(26 * sizeof(int));
    if (alpha_set == NULL)
    {
        perror("Unable to allocate memory for alpha_set");
        exit(1);
    }
    for (int i = 0; i < 26; i++) {
        alpha_set[i] = 0;
    }
    int lines = 0;
    for (char* ch_ptr = buffer; *ch_ptr != '\0'; ch_ptr++)
    {
        if (*ch_ptr == '\n')
        lines++;
        else if (*ch_ptr <= 'z' && *ch_ptr >= 'a')
        {
            if (alpha_set[*ch_ptr - 'a'] > 0)
            {
                printf("Invalid board\n");
                exit(1);
            }
            else
            alpha_set[*ch_ptr - 'a']++;
        }
        else
        {
            printf("Input not from a to z\n");
            exit(1);
        }
    }
    if (lines < 3)
    {
        printf("Invalid board\n");
        exit(1);
    }

    return alpha_set;
}

void parse_board(const char* board, int* letter_row)
{
    int current_row = 0;
    for (const char* p = board; *p; p++)
    {
        if (*p == '\n')
        current_row++;
        else if (*p >= 'a' && *p <= 'z')
        letter_row[*p - 'a'] = current_row;
    }
}


int check_conseq(const char* std_input, const int* letter_row)
{
    char last_char = 0;
    int last_row = -1;
    bool new_word = true;
    for (const char* ch_ptr = std_input; *ch_ptr; ch_ptr++)
    {
        if (*ch_ptr == '\n' || *ch_ptr == ' ')
        {
            new_word = true;
            last_char = 0;
            last_row = -1;
            continue;
        }
        if (!new_word) {
            int current_row = letter_row[*ch_ptr - 'a'];
            if (last_char == *ch_ptr)
            {
                printf("Same-side letter used consecutively\n");
                exit(0);
            }
            if (last_row == current_row && last_row != -1)
            {
                printf("Same-side letter used consecutively\n");
                exit(0);
            }
        }
        last_char = *ch_ptr;
        last_row = letter_row[*ch_ptr - 'a'];
        new_word = false;
    }

    return 0;
}

int check_input(char* std_input, char* file_board, int* alpha_set)
{
    int* alpha_set_std_input = malloc(26 * sizeof(int));
    if (alpha_set_std_input == NULL)
    {
        perror("Unable to allocate memory for alpha_set_std_input");
        exit(1);
    }
    for (int i = 0; i < 26; i++)
    {
        alpha_set_std_input[i] = 0;
    }
    char last_char = 0;
    char first_char = 0;
    bool new_line = true;
    for (char* ch_ptr = std_input; *ch_ptr != '\0'; ch_ptr++)
    {
        if (*ch_ptr == '\n')
        {
            new_line = true;
            continue;
        }
        if (new_line)
        {
            first_char = *ch_ptr;
            new_line = false;
            if (last_char && last_char != first_char)
            {
                printf("First letter of word does not match last letter of previous word\n");
                exit(0);
            }
        }
        if (*ch_ptr >= 'a' && *ch_ptr <= 'z')
        {
            alpha_set_std_input[*ch_ptr - 'a']++;
        }
        else
        {
            printf("Input not from a to z\n");
            exit(1);
        }
        last_char = *ch_ptr;
    }

    for (int i = 0; i < 26; i++)
    {
        if (alpha_set_std_input[i] != 0 && alpha_set[i] == 0)
        {
            printf("Used a letter not present on the board\n");
            exit(0);
        }
        if (alpha_set_std_input[i] == 0 && alpha_set[i] != 0)
        {
            printf("Not all letters used\n");
            exit(0);
        }
    }
    free(alpha_set_std_input);
    int letter_row[26];
    memset(letter_row, -1, sizeof(letter_row));
    parse_board(file_board, letter_row);
    check_conseq(std_input, letter_row);
    return 0;
}


char** split_lines(char *buffer, int *count)
{
    char** lines = NULL;
    int capacity = 0;
    *count = 0;
    char* line = strtok(buffer, "\n");
    while (line)
    {
        if (*count >= capacity)
        {
            capacity = (capacity == 0) ? 1 : capacity * 2;
            lines = realloc(lines, capacity * sizeof(char*));
        }
        lines[*count] = line;
        (*count)++;
        line = strtok(NULL, "\n");
    }
    return lines;
}

int binary_search(char **array, int size, const char *key)
{
    int low = 0, high = size - 1;
    while (low <= high)
    {
        int mid = low + (high - low) / 2;
        int res = strcmp(array[mid], key);
        if (res == 0)
        return 1;
        else if (res < 0)
        low = mid + 1;
        else
        high = mid - 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Wrong number of arguments\n");
        exit(1);
    }
    char* file_board = read_file_dynamically(argv[1]);
    char *file_dict = read_file_dynamically(argv[2]);
    char* std_input = read_stdin();
    int* alpha_set = check_board(file_board);
    check_input(std_input, file_board, alpha_set);
    int dict_size, std_input_size;
    char** dict_words = split_lines(file_dict, &dict_size);
    char** input_words = split_lines(std_input, &std_input_size);
    for (int i = 0; i < std_input_size; i++) {
        if (!(binary_search(dict_words, dict_size, input_words[i])))
        {
            printf("Word not found in dictionary\n");
            exit(0);
        }
    }
    free(alpha_set);
    free(file_dict);
    free(std_input);
    free(dict_words);
    free(input_words);
    printf("Correct\n");
    exit(0);
}

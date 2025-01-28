#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/wait.h>
#include <dirent.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAXIMUM_ARGS 10
#define MAXIMUM_PATH 128
#define MAX_PATH_LENGTH 128
#define MAXIMUM_CWD 1024
#define DEFAULT_HISTORY_SIZE 5
#define MAXIMUM_HISTORY 100
#define MAX_COMMAND_LEN 512
#define MAX_PATHS 20


typedef struct ShellVariable \
{
    char name[MAX_COMMAND_LEN];
    char value[MAX_COMMAND_LEN];
    struct ShellVariable* next;
} ShellVariable;
typedef int (*built_in_func)(char** args);
// const char* PATH="/bin/";
struct built_in_command \
{
    const char* key;
    built_in_func value;
};

ShellVariable* shell_vars = NULL;

int built_in_exit(char** args);
int built_in_cd(char** args);
int built_in_export(char** args);
int built_in_local(char** args);
int built_in_vars(char** args);
int built_in_history(char** args);
int built_in_ls(char** args);

struct built_in_command built_ins[] = \
{
    {"exit", built_in_exit},
    {"cd", built_in_cd},
    {"export", built_in_export},
    {"local", built_in_local},
    {"vars", built_in_vars},
    {"history", built_in_history},
    {"ls", built_in_ls}
};

char* find_executable(char*);
char* parse_and_execute(char *);
char* replace_vars_in_token(char*);
char** split_input_to_token(char*);
int loop_propmt(char*);
void exec_fork(char**);
int determine_annotation(char*);
void free_memory(void);
int set_shell_var(char*, char*);
void free_shell_vars(void);
void restore_redirection(void);
int starts_with_special_prefix(char*);
int compare(const void *, const void *);

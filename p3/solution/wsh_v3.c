#include "wsh.h"

static char* path_variable;
static char** history = NULL; // Array to store history commands
static unsigned history_count = 0; // Current number of commands in history
static unsigned history_capacity = DEFAULT_HISTORY_SIZE; // Current history capacity
int stdin_backup, stdout_backup, stderr_backup; // Backup std file

int determine_annotation(char* command_input)
{
    while (*command_input && isspace((unsigned char)* command_input))
    command_input++;
    return ((*command_input == '#') || (*command_input == '\0')); // the whole line is space or started with #
}

void restore_redirection(void)
{
    // 恢复标准输入
    dup2(stdin_backup, 0);
    // 恢复标准输出
    dup2(stdout_backup, 1);
    // 恢复标准错误
    dup2(stderr_backup, 2);
}



char* parse_and_execute(char *input) {
    char *command = NULL;
    char *redirect = NULL;
    int type = 0;
    if (stdin_backup == -1)
    stdin_backup = dup(0);  // 备份标准输入
    if (stdout_backup == -1)
    stdout_backup = dup(1); // 备份标准输出
    if (stderr_backup == -1)
    stderr_backup = dup(2); // 备份标准错误

    // 查找重定向符号
    if ((redirect = strstr(input, "<")) != NULL) {
        type = 1;  // 输入重定向
    } else if ((redirect = strstr(input, ">>")) != NULL) {
        type = 3;  // 输出重定向 (追加)
    } else if ((redirect = strstr(input, ">")) != NULL) {
        type = 2;  // 输出重定向 (覆盖)
    } else if ((redirect = strstr(input, "&>>")) != NULL) {
        type = 5;  // 同时追加重定向标准输出和标准错误
    } else if ((redirect = strstr(input, "&>")) != NULL) {
        type = 4;  // 同时重定向标准输出和标准错误
    }

    if (redirect) {
        // 截断命令部分
        *redirect = '\0';  // 把重定向符号前面的字符串作为命令
        redirect += (type == 3 || type == 4) ? 2 : (type == 5 ? 3 : 1);  // 跳过重定向符号的长度
    }

    // 剩下的是命令部分
    command = input;

    int fd;

    if (type == 1) {
        // 输入重定向
        fd = open(redirect, O_RDONLY);
        if (fd < 0) {
            // perror("redirection failed");
            exit(-1);
        }
        // 重定向标准输入
        dup2(fd, 0);
        close(fd);
    } else if (type == 2) {
        // 输出重定向 (覆盖)
        fd = open(redirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            // perror("redirection failed");
            exit(-1);
        }
        // 重定向标准输出
        dup2(fd, 1);
        close(fd);
    } else if (type == 3) {
        // 输出重定向 (追加)
        fd = open(redirect, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            // perror("redirection failed");
            exit(-1);
        }
        // 重定向标准输出
        dup2(fd, 1);
        close(fd);
    } else if (type == 4) {
        // 同时重定向标准输出和标准错误
        fd = open(redirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            // perror("redirection failed");
            exit(-1);
        }
        // 重定向标准输出和标准错误
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    } else if (type == 5) {
        // 同时追加重定向标准输出和标准错误
        fd = open(redirect, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            // perror("redirection failed");
            exit(-1);
        }
        // 重定向标准输出和标准错误
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }
    return command;

}

char* replace_vars_in_token(char* token)
{
    if (token[0] == '$')
    {
        // 获取变量名（去掉前面的$符号）
        char* var_name = token + 1;

        // 仅提取变量名部分（遇到等号、空格等终止符号时停止）
        char var_buffer[MAXIMUM_ARGS];
        int i = 0;
        while (var_name[i] && (isalnum(var_name[i]) || var_name[i] == '_')) {
            var_buffer[i] = var_name[i];
            i++;
        }
        var_buffer[i] = '\0';  // 添加字符串结束符

        // 查找环境变量
        char* env_value = getenv(var_buffer);  // 直接使用 var_buffer
        if (env_value != NULL) {
            return strdup(env_value);  // 这里返回的字符串需要在之后被 free
        }

        // 查找shell变量
        ShellVariable* curr = shell_vars;
        while (curr != NULL)
        {
            if (strcmp(curr->name, var_buffer) == 0)
            {
                // 返回变量的值替换$A
                return strdup(curr->value);  // 这里返回的字符串也需要在之后被 free
            }
            curr = curr->next;
        }

        // 如果找不到对应的变量，返回空字符串
        return strdup("");  // 同样需要被 free
    }
    // 如果不是$开头的token，返回原始token
    return token;
}

char** split_input_to_token(char* command_input)
{
    char* command_token = strtok(command_input, " ");
    if (command_token == NULL || *command_token == '\0')
    {
        // printf("No command input\n");
        exit(-1);
    }
    char** args = malloc(sizeof(char*) * MAXIMUM_ARGS); // limit the maximum number of the arguments to 10
    if (!args)
    {
        // perror("Malloc space for args failed");
        exit(-1);
    }
    int i = 0;
    args[i++] = command_token;
    while ((command_token = strtok(NULL, " ")) != NULL)
    {
        args[i++] = strdup(replace_vars_in_token(command_token));
    }
    args[i] = NULL; // the list of arguments ends with NULL
    return args;
}

char* find_executable(char* token)
{
    char full_path[MAX_PATH_LENGTH]; // 用于存储拼接后的完整路径
    char* path_copy = strdup(path_variable); // 复制 PATH 变量，避免修改原字符串
    char* path_dir = strtok(path_copy, ":"); // 用冒号分割 PATH
    // 遍历每个路径
    while (path_dir != NULL) {
        // 拼接目录路径和命令（例如 /usr/bin/ls）
        snprintf(full_path, sizeof(full_path), "%s/%s", path_dir, token);
        // 检查拼接后的路径是否可执行
        if (access(full_path, X_OK) == 0) {
            // 动态分配内存并复制拼接好的路径
            char* result = strdup(full_path);
            free(path_copy);  // 释放 path_copy
            return result;    // 返回动态分配的完整路径
        }

        // 获取下一个路径
        path_dir = strtok(NULL, ":");
    }
    if (access(token, X_OK) == 0) {
            // 动态分配内存并复制拼接好的路径
            char* result = strdup(token);
            free(path_copy);  // 释放 path_copy
            return result;    // 返回动态分配的完整路径
        }

    // 释放复制的 PATH 字符串
    free(path_copy);
    return NULL; // 没有找到可执行文件，返回 NULL
}


void exec_fork(char** tokens)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        // perror("Fork subprocess failed");
        exit(-1);
    }
    else if (pid == 0)
    {
        char* full_path;
        full_path = find_executable(tokens[0]);
        if (strcmp(full_path, "") != 0)
        {
            size_t i = 0;
            while (i < MAXIMUM_ARGS && tokens[i] != NULL)
            i++;
            tokens[i] = NULL;
            execv(full_path, tokens);
            free(full_path);
            // perror("execv failed");
            exit(1);
        }
        
        // snprintf(full_path, sizeof(full_path), "%s%s", path_variable, tokens[0]);

        
        // if (!access(full_path, X_OK))
        // {
        //     execv(full_path, tokens);
        //     perror("execv failed for full_path");
        //     exit(1);
        // }
        // else if (!access(tokens[0], X_OK))
        // {
        //     execv(tokens[0], tokens);
        //     perror("execv failed for tokens[0]");
        //     exit(1);
        // }
    }
    else
    {
        int status;
        while (waitpid(pid, &status, 0) == -1) {
            if (errno == EINTR) {
                // 如果系统调用被信号中断，继续等待
                continue;
            } else {
                // 处理waitpid的其他错误
                // perror("waitpid failed");
                break;
            }
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            perror("Command execution incorrectly");
        }
    }
}

int built_in_exit(char** args)
{
    int length = 0;
    for (int i = 0; args[i] != NULL; i++)
    length++;
    if (length > 1)
    {
        // perror("No extra args should be passed to exit");
        return 0;
    }
    free_memory();
    exit(0);
}

int built_in_cd(char** args)
{
    int length = 0;
    for (int i = 0; args[i] != NULL; i++)
    length++;
    if ((length == 1) || (length > 2))
    {
        // perror("Wrong args are passed to cd");
        return 0;
    }
    if (chdir(args[1]) != 0)
    {
        // perror("Built in command cd execute incorrectly");
        return 0;
    }
    return 0;
}

int built_in_export(char** args)
{
    char *name = strtok(args[1], "=");
    char *value = strtok(NULL, "=");

    if (name != NULL && value != NULL)
    setenv(name, value, 1);  // 设置环境变量
    return 0;
}

int built_in_local(char** args)
{
    if (args[1])
    {
        char* var = strtok(args[1], "=");
        char* value = strtok(NULL, "=");
        // if (value==NULL)
        // value = "";
        if (var)
        set_shell_var(var, value);
    }
    return 0;
}

int set_shell_var(char* name, char* value)
{
    ShellVariable* prev = NULL;
    ShellVariable* curr = shell_vars;

    // Traverse the list of variables to check if the variable already exists
    while (curr != NULL)
    {
        if (strcmp(curr->name, name) == 0) // Variable found
        {
            if (value == NULL || strcmp(value, "") == 0) // Empty string, remove the variable
            {
                if (prev == NULL)
                    shell_vars = curr->next; // Removing the first element in the list
                else
                    prev->next = curr->next;
                // free(curr);
                return 0;
            }
            strcpy(curr->value, value); // Update the variable's value if it's not empty
            return 0;
        }
        prev = curr;
        curr = curr->next;
    }

    // If variable does not exist and the value is empty, don't add it
    if (value == NULL || strcmp(value, "") == 0) {
        return 0;
    }

    // If variable is new, create it
    ShellVariable* new_var = malloc(sizeof(ShellVariable));
    if (!new_var)
    {
        // perror("Failed to allocate memory for new shell variable");
        exit(-1);
    }
    strcpy(new_var->name, name);
    strcpy(new_var->value, value);
    new_var->next = NULL;

    // Insert the new variable at the end of the list
    if (shell_vars == NULL) {
        shell_vars = new_var; // If list is empty, set the first variable
    } else {
        ShellVariable* tail = shell_vars;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = new_var; // Add the new variable at the end of the list
    }

    return 0;
}

void free_shell_vars(void)
{
    ShellVariable* curr = shell_vars;
    while (curr != NULL)
    {
        ShellVariable* temp = curr;
        curr = curr->next;
        free(temp);
    }
}

int built_in_vars(char** args)
{
    unsigned length=0; // adjust to input args
    while (args[length] != NULL)
    length++;
    ShellVariable* curr = shell_vars;
    while (curr != NULL)
    {
        printf("%s=%s\n", curr->name, curr->value);
        curr = curr->next;
    }
    return 0;
}

int built_in_history(char** args)
{
    unsigned length=0;
    while (args[length] != NULL)
    length++;
    if (length == 1)
    {
        if (history_count == 0)
        return 0;
        unsigned counter = 1;
        int start_record = history_count - 1;
        for (int i = start_record; i >= 0; i--)
        {
            printf("%u) %s\n", counter, history[i]);
            counter++;
        }
    }
    else if (length == 2)
    {
        unsigned exec_num = strtoul(args[1], NULL, 10);
        if (exec_num == 0 || exec_num > history_count)
        return 0; // do nothing, keep prompting

        unsigned counter = 1;
        int start_record = history_count - 1;
        for (int i = start_record; i >= 0; i--)
        {
            if (exec_num == counter)
            {
                char* command_to_execute = strdup(history[i]);
                char** tokens;
                tokens = split_input_to_token(command_to_execute);
                // free(command_to_execute);
                exec_fork(tokens);
                for (int j = 0; tokens[j] != NULL; j++) {
                    free(tokens[j]);  // 释放通过 strdup 分配的字符串
                }
                free(tokens);  // 释放tokens本身
            }

            counter++;
        }
        }
    else if (length == 3)
    {
        unsigned new_history_capacity = strtoul(args[2], NULL, 10);
        // if get wrong new size
        if (new_history_capacity == 0 || new_history_capacity > MAXIMUM_HISTORY)
        {
            // perror("Invalid history size");
            return 0;
        }
        // if shrinking the history, free the extra commands
        if (new_history_capacity < history_count)
        {
            for (unsigned i = 0; i < history_count - new_history_capacity; i++)
            // free(history[i]);
            for (unsigned i = history_count - new_history_capacity; i < history_count; i++)
            history[i - (history_count - new_history_capacity)] = history[i];
            history[history_capacity] = NULL;
            history_count = new_history_capacity;
        }
        history_capacity = new_history_capacity;
    }
    else
    {
        // perror("Wrong args are passed to history");
        return 0;
    }
    return 0;
}

int built_in_ls(char** args)
{
    int length = 0;
    for (int i = 0; args[i] != NULL; i++)
    length++;
    if (length > 1)
    {
        // perror("No extra args should be passed to ls");
        return 0;
    }
    // imitate ls in shell
    char cwd[MAXIMUM_CWD];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        // perror("ls execute incorrectly when getcwd()");
        return 0;
    }
    DIR *dir;
    struct dirent* entry;
    dir = opendir(cwd);
    if (dir == NULL) {
        // perror("Opendir failed");
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) 
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) // skip . and ..
        continue;
        printf("%s\n", entry->d_name); // read and print the contents of the directory
    }
    closedir(dir);
    return 0;
}



int handle_built_in_command(char* input)
{
    
    char** tokens = split_input_to_token(input);
    for (size_t i = 0; i < (sizeof(built_ins) / sizeof(struct built_in_command)); i++)
    {
        if (strcmp(tokens[0], built_ins[i].key) == 0)
        {
            int built_ins_output = (built_ins[i].value)(tokens);
            return built_ins_output;
        }
    }
    return 1;
}


int loop_propmt(char* prompt_tag)
{
    char input[MAXIMUM_PATH]; // limit the length of input
    char* get_input;
    history = (char**)malloc(MAXIMUM_HISTORY * sizeof(char*)); // set maximum records of history
    // imitate the shell
    while (1)
    {
        if (strcmp(prompt_tag, "prompt") == 0)
        printf("wsh> "); // loop output
        get_input = fgets(input, sizeof(input), stdin); //get input from stdin
        if (get_input == NULL) // EOF or error
        exit(0);
        input[strcspn(input, "\n")] = 0;
        if (determine_annotation(input)) // judge if this is an annotation
        continue;
        char *command = parse_and_execute(input);
        char* ori_input = strdup(input);
        int handle_built_in_command_output = handle_built_in_command(command); // judge if this is a built-in command
        if (handle_built_in_command_output == 1) // if not, fork
        {
            if (history_count < history_capacity)
            {
                history[history_count] = strdup(ori_input);
                history_count++;
            }
            else
            {
                free(history[0]);
                for (unsigned i = 1; i < history_capacity; i++)
                history[i - 1] = history[i];
                history[history_capacity-1] = strdup(ori_input);
            }
            char** tokens;
            tokens = split_input_to_token(ori_input);
            exec_fork(tokens);
            for (int j = 0; tokens[j] != NULL; j++) {
                free(tokens[j]);  // 释放通过 strdup 分配的字符串
            }
            free(tokens);  // 释放tokens本身
        }
        // printf("input: %s\n", input);
        // printf("command: %s\n", command);
        if (strcmp(input, command) != 0)
        {
            // printf("here");
            restore_redirection();
        }
    }
}




void free_memory(void)
{
    free(path_variable);
    free_shell_vars();
    if (stdin_backup != -1) {
        close(stdin_backup);  // 释放备份的文件描述符
        stdin_backup = -1;  // 重置备份标志
    }
    if (stdout_backup != -1) {
        close(stdout_backup);  // 释放备份的文件描述符
        stdout_backup = -1;  // 重置备份标志
    }
    if (stderr_backup != -1) {
        close(stderr_backup);  // 释放备份的文件描述符
        stderr_backup = -1;  // 重置备份标志
    }
    // Free memory used by history
    for (unsigned i = 0; i < history_count; i++)
    free(history[i]);
    free(history);
}

int starts_with_special_prefix(char *str)
{
    if (strncmp(str, "<", 1) == 0) {
        return 1;
    } else if (strncmp(str, ">>", 2) == 0) {
        return 1;
    } else if (strncmp(str, ">", 1) == 0) {
        return 1;
    } else if (strncmp(str, "&>>", 3) == 0) {
        return 1;
    } else if (strncmp(str, "&>", 2) == 0) {
        return 1;
    }
    return 0;
}



int main(int argc, char* argv[])
{
    // 禁用 stdout 缓冲区
    setvbuf(stdout, NULL, _IONBF, 0);
    path_variable = strdup(getenv("PATH"));
    if (!(isatty(fileno(stdin)))) // if redirection?
    {
        loop_propmt("prompt");
    }
    else
    {
        if (argc == 1)
        {
            loop_propmt("prompt");
        }
        else if (argc == 2)
        {
            // batch file
            FILE* file = fopen(argv[1], "r");
            if (file == NULL)
            {
                // printf("Can not open the file path");
                exit(-1); 
            }
            if (freopen(argv[1], "r", stdin) == NULL)  // replace stdin with this file
            {
                // perror("Failed to redirect stdin");
                exit(EXIT_FAILURE);  // 如果文件打开失败，退出程序
            }
            if (starts_with_special_prefix(argv[1]))
            {
                loop_propmt("prompt");
            }
            else
            {
                loop_propmt("no prompt"); // no wsh> prompt output
            }
            
            if (freopen("/dev/tty", "r", stdin) == NULL)
            {
                // perror("Failed to restore stdin");
                exit(EXIT_FAILURE);  // 如果恢复失败，退出程序
            } // turn to the normal stdin file
        }
        else
        {
            // input args incorrectly
            // printf("Invalid argc, please only include the name of batch file");
            exit(-1);
        }
    }
}

#include "wsh.h"

// static char* path_variable;
static char** history = NULL; // Array to store history commands
static unsigned history_count = 0; // Current number of commands in history
static unsigned history_capacity = DEFAULT_HISTORY_SIZE; // Current history capacity
int stdin_backup, stdout_backup, stderr_backup; // Backup std file
static int last_exit_status = 0;

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
    setvbuf(stdout, NULL, _IONBF, 0);
}



char* parse_and_execute(char *input) {
    char *command = NULL;
    char *redirect = NULL;
    int type = 0;
    int fd;
    int redirect_fd = 1;  // 默认重定向的文件描述符是标准输出

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

    // 处理数字重定向的情况
    if (redirect && redirect > input && *(redirect - 1) >= '0' && *(redirect - 1) <= '9') {
        redirect_fd = *(redirect - 1) - '0';  // 获取重定向的文件描述符
        *(redirect - 1) = '\0';  // 删除重定向符号前的数字
    }

    if (redirect) {
        *redirect = '\0';  // 截断命令部分
        redirect += (type == 3 || type == 4) ? 2 : (type == 5 ? 3 : 1);  // 跳过重定向符号的长度
    }

    // 剩下的是命令部分
    command = input;

    if (type == 1) {
        // 输入重定向
        fd = open(redirect, O_RDONLY);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
        dup2(fd, 0);
        close(fd);
    } else if (type == 2) {
        // 输出重定向 (覆盖)
        fd = open(redirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
        dup2(fd, redirect_fd);  // 重定向到指定文件描述符
        close(fd);
    } else if (type == 3) {
        // 输出重定向 (追加)
        fd = open(redirect, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
        dup2(fd, redirect_fd);  // 重定向到指定文件描述符
        close(fd);
    } else if (type == 4) {
        // 同时重定向标准输出和标准错误
        fd = open(redirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    } else if (type == 5) {
        // 同时追加重定向标准输出和标准错误
        fd = open(redirect, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
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
        // const char *var_name_dup = strdup(var_name);  // 跳过 $
        // char *env_value = getenv(var_name_dup);  // 优先查找环境变量
        // if (env_value != NULL) {
        //     char* var_value = strdup(env_value);
        //     free(var_name_dup);
        //     return var_value;
        // }
        char *var_name_dup = strdup(var_name);  // 跳过 $
        // char *env_value = getenv(var_name_dup);  // 优先查找环境变量
        // char *env_value = getenv(var_buffer);  // 优先查找环境变量
        // char *env_value = strdup(path_variable);
        char *env_value = strdup(getenv("PATH"));
        if (env_value != NULL) {
            // char* var_value = strdup(env_value);
            // free(var_name_dup);  // 释放动态分配的内存
            // return var_value;

            char* result = malloc(strlen(env_value) + strlen(var_name + i) + 1);
            sprintf(result, "%s%s", env_value, var_name + i);
            free(var_name_dup);
            return result;
        }
        free(env_value);

        // 查找shell变量
        ShellVariable* curr = shell_vars;
        while (curr != NULL)
        {
            if (strcmp(curr->name, var_buffer) == 0)
            {
                // 返回变量的值替换$A
                // free(var_name_dup);
                // return curr->value;

                char* result = malloc(strlen(curr->value) + strlen(var_name + i) + 1);
                sprintf(result, "%s%s", curr->value, var_name + i);
                free(var_name_dup);
                return result;
            }
            curr = curr->next;
        }
        free(var_name_dup);
        // 如果找不到对应的变量，返回空字符串
        return strdup("");
    }
    // 如果不是$开头的token，返回原始token
    return strdup(token);
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
        last_exit_status = -1;
        exit(-1);
    }
    int i = 0;
    args[i++] = command_token;
    while ((command_token = strtok(NULL, " ")) != NULL)
    {

        char* replaced_token = replace_vars_in_token(command_token);
        args[i++] = strdup(replaced_token);
        free(replaced_token);  // 释放 replace_vars_in_token 返回的动态内存

        // args[i++] = strdup(replace_vars_in_token(command_token));
    }
    args[i] = NULL; // the list of arguments ends with NULL
    return args;
}

char* find_executable(char* token)
{
    char full_path[MAX_PATH_LENGTH]; // 用于存储拼接后的完整路径
    // char* path_copy = strdup(path_variable); // 复制 PATH 变量，避免修改原字符串
    char* path_copy = strdup(getenv("PATH"));
    char* path_dir = strtok(path_copy, ":"); // 用冒号分割 PATH
    char* result = NULL; // 用于存储最终的结果

    // 遍历每个路径
    while (path_dir != NULL) {
        // 拼接目录路径和命令（例如 /usr/bin/ls）
        snprintf(full_path, sizeof(full_path), "%s/%s", path_dir, token);
        // 检查拼接后的路径是否可执行
        if (access(full_path, X_OK) == 0) {
            // 动态分配内存并复制拼接好的路径
            result = strdup(full_path);
            break; // 找到可执行文件，退出循环
        }
        // 获取下一个路径
        path_dir = strtok(NULL, ":");
    }
    if (access(token, X_OK) == 0) {
        // 动态分配内存并复制拼接好的路径
        result = strdup(token);
    }
    if (result == NULL)
    last_exit_status = -1;
    // 释放复制的 PATH 字符串
    free(path_copy);
    return result; // 返回找到的路径或 NULL
}


void exec_fork(char** tokens)
{

    char* full_path;
    full_path = find_executable(tokens[0]);
    if (full_path == NULL)
    {
        last_exit_status = -1;
    }
    else
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            // perror("Fork subprocess failed");
            last_exit_status = -1;
            for (int j = 1; tokens[j] != NULL; j++)
            {
                free(tokens[j]);
            }
            free(tokens);
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

                for (int j = 1; tokens[j] != NULL; j++)
                {
                    free(tokens[j]);
                }
                free(tokens);
                free(full_path);
                // perror("execv failed");
                last_exit_status = -1;
                exit(-1);
            }
            
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
                    last_exit_status = -1;
                    break;
                }
            }

            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                perror("Command execution incorrectly");
                last_exit_status = -1;
            }
            for (int j = 1; tokens[j] != NULL; j++)
            {
                free(tokens[j]);
            }
            free(tokens);
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
        last_exit_status = -1;
        return 0;
    }
    free_memory();
    exit(last_exit_status);
}

int built_in_cd(char** args)
{
    int length = 0;
    for (int i = 0; args[i] != NULL; i++)
    length++;
    if ((length == 1) || (length > 2))
    {
        // perror("Wrong args are passed to cd");
        last_exit_status = -1;
        return 0;
    }
    if (chdir(args[1]) != 0)
    {
        // perror("Built in command cd execute incorrectly");
        last_exit_status = -1;
        return 0;
    }
    last_exit_status = 0;
    return 0;
}

int built_in_export(char** args)
{
    char *name = strtok(args[1], "=");
    char *value = strtok(NULL, "=");

    if (name != NULL && value != NULL)
    {
        // setenv(name, value, 1);  // 设置环境变量
        if (setenv(name, value, 1) == 0) {
            return 0;  // 成功
        }
        else {
            // perror("setenv failed");  // 输出错误信息
            last_exit_status = -1;
            return -1;  // 失败
        }
    }
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
                free(curr);
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
        last_exit_status = -1;
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
                free(command_to_execute);
                exec_fork(tokens);
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
            last_exit_status = -1;
            return 0;
        }
        // if shrinking the history, free the extra commands
        if (new_history_capacity < history_count)
        {
            // for (unsigned i = 0; i < history_count - new_history_capacity; i++)
            // free(history[i]);
            // for (unsigned i = history_count - new_history_capacity; i < history_count; i++)
            // history[i - (history_count - new_history_capacity)] = history[i];
            // history[history_capacity] = NULL;
            // history_count = new_history_capacity;
            for (unsigned i = 0; i < history_count - new_history_capacity; i++) {
                free(history[i]);
            }
            // Move the remaining commands to the front of the array
            for (unsigned i = history_count - new_history_capacity; i < history_count; i++) {
                history[i - (history_count - new_history_capacity)] = history[i];
            }
            history_count = new_history_capacity;
            // Resize the history array to the new capacity
            char** new_history = realloc(history, new_history_capacity * sizeof(char*));
            if (new_history == NULL) {
                return 0; // Handle realloc failure, the old memory is still valid, so no leak here
            }
            history = new_history;
        }
        history_capacity = new_history_capacity;
    }
    else
    {
        // perror("Wrong args are passed to history");
        last_exit_status = -1;
        return 0;
    }
    return 0;
}

int compare(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int built_in_ls(char** args)
{
    int length = 0;
    for (int i = 0; args[i] != NULL; i++)
        length++;
    if (length > 1)
    {
        // perror("No extra args should be passed to ls");
        last_exit_status = -1;
        return 0;
    }

    // 模拟 ls 命令
    char cwd[MAXIMUM_CWD];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        // perror("ls execute incorrectly when getcwd()");
        last_exit_status = -1;
        return 0;
    }

    DIR *dir;
    struct dirent* entry;
    dir = opendir(cwd);
    if (dir == NULL) {
        // perror("Opendir failed");
        last_exit_status = -1;
        return 0;
    }

    // 数组存储文件名
    char *filenames[1024];  // 假设目录中不超过1024个文件
    int count = 0;

    // 读取目录中的条目并存储在数组中
    while ((entry = readdir(dir)) != NULL) 
    {
        // 跳过 . 和 .. 以及 .DS_Store 和 .gitignore 文件
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".DS_Store") == 0 || strcmp(entry->d_name, ".gitignore") == 0) 
        {
            continue;  // 跳过这些文件
        }

        // 存储文件名
        filenames[count] = strdup(entry->d_name);  // 复制文件名到数组中
        count++;
    }

    // 关闭目录
    closedir(dir);

    // 对文件名进行排序
    qsort(filenames, count, sizeof(char *), compare);

    // 输出排序后的文件名
    for (int i = 0; i < count; i++) {
        printf("%s\n", filenames[i]);  // 输出文件名
        free(filenames[i]);  // 释放动态分配的内存
    }

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
            for (int j = 1; tokens[j] != NULL; j++)
            {
                free(tokens[j]);
            }
            free(tokens);
            return built_ins_output;
        }
    }
    for (int j = 1; tokens[j] != NULL; j++)
    {
        free(tokens[j]);
    }
    free(tokens);
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
        {
            // exit(0);
            exit(last_exit_status);
        }
        input[strcspn(input, "\n")] = 0;
        // input = replace_vars_in_token(input);
        char *new_input = replace_vars_in_token(input);
        if (determine_annotation(new_input)) // judge if this is an annotation
        continue;
        char *command = parse_and_execute(new_input);
        char* ori_input = strdup(new_input);
        int handle_built_in_command_output = handle_built_in_command(command); // judge if this is a built-in command
        if (handle_built_in_command_output == 1) // if not, fork
        {
            if (history_count < history_capacity)
            {

                char* ori_input_dup = strdup(ori_input);
                history[history_count] = strdup(ori_input_dup);
                free(ori_input_dup); 
                history_count++;

                // history[history_count] = strdup(ori_input);
                // history_count++;
            }
            else
            {
                free(history[0]);
                for (unsigned i = 1; i < history_capacity; i++)
                history[i - 1] = history[i];
                char* ori_input_dup = strdup(ori_input);
                history[history_capacity-1] = strdup(ori_input_dup);
                free(ori_input_dup); 
                // history[history_capacity-1] = strdup(ori_input);
            }
            char** tokens;
            tokens = split_input_to_token(ori_input);
            
            exec_fork(tokens);

            restore_redirection();
        }
        
        // printf("input: %s\n", input);
        // printf("command: %s\n", command);
        // if (strcmp(new_input, command) != 0)
        // {
        //     // printf("here");
        //     restore_redirection();
        // }
        free(new_input);
        free(ori_input);
    }
    last_exit_status = -1;
}




void free_memory(void)
{
    // free(path_variable);
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
    clearenv();
    setenv("PATH", "/bin", 1);
    struct stat statbuf;
    // path_variable = strdup(getenv("PATH"));
    if (!(isatty(fileno(stdin)))) // if redirection?
    {
        if (fstat(fileno(stdin), &statbuf) == -1)
        {
            // perror("fstat");
            last_exit_status = -1;
            return last_exit_status;
        }
        if (S_ISREG(statbuf.st_mode))
        {
            loop_propmt("prompt");
        }
        // 检查输入是否来自管道（Here String会是管道或者临时文件）
        else
        {
            loop_propmt("no prompt");
        }
        // loop_propmt("prompt");
        return last_exit_status;
    }
    else
    {
        if (argc == 1)
        {
            loop_propmt("prompt");
            return last_exit_status;
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
                fclose(file);
                // free(path_variable); 
                exit(EXIT_FAILURE);  // 如果文件打开失败，退出程序
                return 0;
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
                fclose(file);
                // free(path_variable); 
                exit(EXIT_FAILURE);  // 如果恢复失败，退出程序
            } // turn to the normal stdin file
            // free(path_variable); 
            fclose(file);
            free_memory();
            return last_exit_status;
        }
        else
        {
            // free(path_variable); 
            // input args incorrectly
            // printf("Invalid argc, please only include the name of batch file");
            exit(-1);
        }
    }
}

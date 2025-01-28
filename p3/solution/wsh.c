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
    return ((*command_input == '#') || (*command_input == '\0')); // The whole line is space or started with #
}

void restore_redirection(void)
{
    // Restore stdin
    dup2(stdin_backup, 0);
    // Restore stdout
    dup2(stdout_backup, 1);
    // Restore stderr
    dup2(stderr_backup, 2);
}



char* parse_and_execute(char *input) {
    char *command = NULL;
    char *redirect = NULL;
    int type = 0;
    int fd;
    int redirect_fd = 1;  // 默认重定向的文件描述符是标准输出

    if (stdin_backup == -1)
        stdin_backup = dup(0);  // Backup stdin
    if (stdout_backup == -1)
        stdout_backup = dup(1); // Backup stdout
    if (stderr_backup == -1)
        stderr_backup = dup(2); // Backup stderr

    // Redirection signal
    if ((redirect = strstr(input, "<")) != NULL) {
        type = 1;
    } else if ((redirect = strstr(input, ">>")) != NULL) {
        type = 3;
    } else if ((redirect = strstr(input, ">")) != NULL) {
        type = 2;
    } else if ((redirect = strstr(input, "&>>")) != NULL) {
        type = 5;
    } else if ((redirect = strstr(input, "&>")) != NULL) {
        type = 4;
    }

    // Dealing with number redirection
    if (redirect && redirect > input && *(redirect - 1) >= '0' && *(redirect - 1) <= '9') {
        redirect_fd = *(redirect - 1) - '0';  // Translate it to int
        *(redirect - 1) = '\0';  // Delete it
    }

    if (redirect) {
        *redirect = '\0';  // Split the command by \0
        redirect += (type == 3 || type == 4) ? 2 : (type == 5 ? 3 : 1);  // Jump over the redirection signal
    }

    // This is the real command
    command = input;

    if (type == 1) {
        // Rewrite stdin
        fd = open(redirect, O_RDONLY);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
        dup2(fd, 0);
        close(fd);
    } else if (type == 2) {
        // Rewrite stdout
        fd = open(redirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
        dup2(fd, redirect_fd);  // redirect to special file
        close(fd);
    } else if (type == 3) {
        // Add to stdout
        fd = open(redirect, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
        dup2(fd, redirect_fd);  // redirect to special file
        close(fd);
    } else if (type == 4) {
        // Rewrite stderr and stdout
        fd = open(redirect, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            last_exit_status = -1;
            exit(-1);
        }
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    } else if (type == 5) {
        // Add to stderr and stdout
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
        // Get rid of $
        char* var_name = token + 1;
        
        // Only extract variable name
        char var_buffer[MAXIMUM_ARGS];
        int i = 0;
        while (var_name[i] && (isalnum(var_name[i]) || var_name[i] == '_')) {
            var_buffer[i] = var_name[i];
            i++;
        }
        var_buffer[i] = '\0';  // Split it by adding \0
        char *var_name_dup = strdup(var_name);  // 跳过 $
        // Look for variable in environ variable first
        if (getenv(var_name_dup))
        {
            char *env_value = strdup(getenv(var_name_dup));
            free(var_name_dup);
            return env_value;
        }
        // Look for variable in shell variable
        ShellVariable* curr = shell_vars;
        while (curr != NULL)
        {
            if (strcmp(curr->name, var_buffer) == 0)
            {
                // Return value of variable to replace $A
                char* result = malloc(strlen(curr->value) + strlen(var_name + i) + 1);
                sprintf(result, "%s%s", curr->value, var_name + i);
                free(var_name_dup);
                return result;
            }
            curr = curr->next;
        }
        free(var_name_dup);
        // If can not find, return empty string
        return strdup("");
    }
    // If token starts with something other than $，return original token
    return strdup(token);
}

char* replace_vars_in_first_token(char* token)
{
    if (token[0] == '$')
    {
        // Get rid of $
        char* var_name = token + 1;
        // Only extract variable name
        int i = 0;
        // Look for variable in environ variable
        char *var_name_dup = strdup(var_name);
        char *env_value = strdup(getenv("PATH"));
        if (env_value != NULL) {
            char* result = malloc(strlen(env_value) + strlen(var_name + i) + 1);
            sprintf(result, "%s%s", env_value, var_name + i);
            free(var_name_dup);
            free(env_value);
            return result;
        }
        free(env_value);
        free(var_name_dup);
        // If can not find, return empty string
        return strdup("");
    }
    // If token starts with something other than $，return original token
    return strdup(token);
}




char** split_input_to_token(char* command_input)
{
    // Split char* to char**
    char* command_token = strtok(command_input, " ");
    if (command_token == NULL || *command_token == '\0')
    {
        // printf("No command input\n");
        exit(-1);
    }
    char** args = malloc(sizeof(char*) * MAXIMUM_ARGS); // Limit the maximum args to 10
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
        free(replaced_token);  // Free replace_vars_in_token

        // args[i++] = strdup(replace_vars_in_token(command_token));
    }
    args[i] = NULL; // The list of arguments ends with NULL
    return args;
}

char* find_executable(char* token)
{
    char full_path[MAX_PATH_LENGTH]; // Store the full path of concatting
    // char* path_copy = strdup(path_variable);
    char* path_copy = strdup(getenv("PATH"));
    char* path_dir = strtok(path_copy, ":"); // Split PATH with :
    char* result = NULL; // Final result

    while (path_dir != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", path_dir, token); // Concat path with command（e.g. /usr/bin/ls）
        if (access(full_path, X_OK) == 0) {
            result = strdup(full_path);
            break;
        }
        // Get next path
        path_dir = strtok(NULL, ":");
    }
    if (access(token, X_OK) == 0) {
        result = strdup(token);
    }
    if (result == NULL)
    last_exit_status = -1;
    free(path_copy);
    return result;
}


void exec_fork(char** tokens)
{

    char* full_path;
    full_path = find_executable(tokens[0]);
    if (full_path == NULL)
    {
        free(full_path);
        last_exit_status = -1;
        for (int j = 1; tokens[j] != NULL; j++)
        {
            free(tokens[j]);
        }
        free(tokens);
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
            else
            free(full_path);
        }
        else
        {
            int status;
            while (waitpid(pid, &status, 0) == -1) {
                if (errno == EINTR) {
                    // Keep waiting if it was interrupted by signal
                    continue;
                } else {
                    // Deal with other error of waitpid
                    // perror("waitpid failed");
                    last_exit_status = -1;
                    break;
                }
            }

            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                // perror("Command execution incorrectly");
                last_exit_status = -1;
            }
            else
                last_exit_status = 0;
            for (int j = 1; tokens[j] != NULL; j++)
            {
                free(tokens[j]);
            }
            free(tokens);
            free(full_path);
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
        // setenv(name, value, 1);  // Set environ variable
        if (setenv(name, value, 1) == 0) {
            last_exit_status = 0;
            return 0;
        }
        else {
            // perror("setenv failed");
            last_exit_status = -1;
            return -1;
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
        {
            set_shell_var(var, value);
            last_exit_status = 0;
        }
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
    unsigned length=0; // Adjust to input args
    while (args[length] != NULL)
    length++;
    ShellVariable* curr = shell_vars;
    while (curr != NULL)
    {
        printf("%s=%s\n", curr->name, curr->value);
        curr = curr->next;
    }
    last_exit_status = 0;
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
        last_exit_status = 0;
    }
    else if (length == 2)
    {
        unsigned exec_num = strtoul(args[1], NULL, 10);
        if (exec_num == 0 || exec_num > history_count)
        return 0; // Do nothing, keep prompting

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
        // If get wrong new size
        if (new_history_capacity == 0 || new_history_capacity > MAXIMUM_HISTORY)
        {
            // perror("Invalid history size");
            last_exit_status = -1;
            return 0;
        }
        // If shrinking the history, free the extra commands
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

    // Imitate ls
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

    // Build an array to store those file names
    char *filenames[1024];  // Assume that no more than 1024 files
    int count = 0;

    // Read and save file names to array
    while ((entry = readdir(dir)) != NULL) 
    {
        // jump . and .. and .DS_Store and .gitignore
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".DS_Store") == 0 || strcmp(entry->d_name, ".gitignore") == 0) 
        {
            continue;
        }
        filenames[count] = strdup(entry->d_name);
        count++;
    }
    closedir(dir);

    // Sort file names
    qsort(filenames, count, sizeof(char *), compare);

    for (int i = 0; i < count; i++) {
        printf("%s\n", filenames[i]);
        free(filenames[i]);
    }
    last_exit_status = 0;
    return 0;
}




int handle_built_in_command(char* input)
{
    // Dealing with built-in command
    char** tokens = split_input_to_token(input);
    for (size_t i = 0; i < (sizeof(built_ins) / sizeof(struct built_in_command)); i++)
    {
        // Match tokens[0] with keys in dictionary
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
    // Free space
    for (int j = 1; tokens[j] != NULL; j++)
    {
        free(tokens[j]);
    }
    free(tokens);
    return 1;
}


int loop_propmt(char* prompt_tag)
{
    char input[MAXIMUM_PATH]; // Limit the length of input
    char* get_input;
    history = (char**)malloc(MAXIMUM_HISTORY * sizeof(char*)); // Set maximum records of history
    // Imitate the shell
    while (1)
    {
        if (strcmp(prompt_tag, "prompt") == 0)
        printf("wsh> "); // loop output
        get_input = fgets(input, sizeof(input), stdin); //Get input from stdin
        if (get_input == NULL) // EOF or error
        {
            // exit(0);
            exit(last_exit_status);
        }
        input[strcspn(input, "\n")] = 0;
        // input = replace_vars_in_token(input);
        // char *new_input = replace_vars_in_token(input);
        char *new_input = replace_vars_in_first_token(input);
        if (determine_annotation(new_input)) // Judge if this is an annotation
        continue;
        char *command = parse_and_execute(new_input);
        char* ori_input = strdup(new_input);
        int handle_built_in_command_output = handle_built_in_command(command); // Judge if this is a built-in command
        if (handle_built_in_command_output == 1) // If not, fork
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

            // if (strcmp(new_input, command) != 0)
            // {
            //     restore_redirection();
            // }
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
    restore_redirection();
}




void free_memory(void)
{
    // free(path_variable);
    free_shell_vars();
    if (stdin_backup != -1) {
        close(stdin_backup);  // Close stdin backup
        stdin_backup = -1;  // Reset tag
    }
    if (stdout_backup != -1) {
        close(stdout_backup);  // Close stdout backup
        stdout_backup = -1;  // Reset tag
    }
    if (stderr_backup != -1) {
        close(stderr_backup);  // Close stderr backup
        stderr_backup = -1;  // Reset tag
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
    // Prohibit stdout flush
    setvbuf(stdout, NULL, _IONBF, 0);
    clearenv();
    setenv("PATH", "/bin", 1);
    struct stat statbuf;
    // path_variable = strdup(getenv("PATH"));
    if (!(isatty(fileno(stdin)))) // If redirection?
    {
        if (fstat(fileno(stdin), &statbuf) == -1)
        {
            // perror("fstat");
            last_exit_status = -1;
            return last_exit_status;
        }
        // If file of string
        if (S_ISREG(statbuf.st_mode))
        {
            loop_propmt("prompt");
        }
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
            // Batch file
            FILE* file = fopen(argv[1], "r");
            if (file == NULL)
            {
                // printf("Can not open the file path");
                exit(-1); 
            }

            if (freopen(argv[1], "r", stdin) == NULL)  // Replace stdin with this file
            {
                // perror("Failed to redirect stdin");
                fclose(file);
                // free(path_variable); 
                exit(EXIT_FAILURE);
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
                exit(EXIT_FAILURE);
            } // Turn to the normal stdin file
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

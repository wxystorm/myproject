#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 32
#define MAX_CMDS 8
#define CODEGREEN "\033[1;32m"
#define CODERED "\033[1;31m"
#define COLORRESET "\033[0m"
typedef struct {
    char *argv[MAX_ARGS + 1];
    int argc;
} command_t;

typedef struct {
    command_t commands[MAX_CMDS];
    int count;
} pipeline_t;

void print_prompt(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return;
    }
    /* Print the prompt here - you can use ANSI escape codes
     *  to make it colorful if you like.
     */
    printf(CODEGREEN "myshell$ " COLORRESET);
    fflush(stdout);
}

int read_line(char **line, size_t *capacity) {
    while (1) {
        errno = 0;
        ssize_t nread = getline(line, capacity, stdin);
        if (nread >= 0) {
            if (nread > 0 && (*line)[nread - 1] == '\n') {
                (*line)[nread - 1] = '\0';
            }
            return 1;
        }

        if (feof(stdin)) {
            return 0;
        }
        if (errno == EINTR) {
            clearerr(stdin);
            continue;
        }

        perror("getline");
        return 0;
    }
}

int is_blank_line(const char *line) {
    while (*line != '\0') {
        if (*line != ' ' && *line != '\t') {
            return 0;
        }
        ++line;
    }
    return 1;
}

/* Return 1 to exit the shell, 0 to continue. */
int handle_line(char *line) {
    /*
     * TODO(student):
     * - parse at most MAX_CMDS commands and MAX_ARGS args per command
     * - reject invalid pipelines before forking
     * - run builtins in the shell process
     * - on exec failure, print to stderr and call _exit(1) in the child
     * - close unused pipe fds in both parent and children
     * - wait for every child you create
     *
     * The structs above are only suggestions.
     */
   

    if (is_blank_line(line)) {
        return 0;
    }

    // 语法检查：拒绝首尾带有管道符的无效输入
    if(line[0] == '|') {
        fprintf(stderr, "Invalid pipeline: starts with a pipe\n");
        return 0;
    }
    if(line[strlen(line) - 1] == '|') {
        fprintf(stderr, "Invalid pipeline: ends with a pipe\n");
        return 0;
    }

    pipeline_t pl;
    pl.count = 0;

    
    char *saveptr;
    char *pipe_line = strtok_r(line, "|", &saveptr);

    while(pipe_line != NULL) {
        if(is_blank_line(pipe_line)) {
            fprintf(stderr, "Invalid pipeline: empty command\n");
            return 0;
        }

        // 检查命令数量限制
        if (pl.count >= MAX_CMDS) {
            fprintf(stderr, "Too many commands\n");
            return 0;
        }

        command_t *cmd = &pl.commands[pl.count];
        cmd->argc = 0;

        char *saveptr2;
        char *arg = strtok_r(pipe_line, " \t\r\n", &saveptr2);
        
        // 拆分参数
        while(arg != NULL) {
            cmd->argv[cmd->argc++] = arg;
            if(cmd->argc >= MAX_ARGS) {
                fprintf(stderr, "Too many arguments\n");
                return 0;
            }
            arg = strtok_r(NULL, " \t\r\n", &saveptr2);
        }
        cmd->argv[cmd->argc] = NULL; // 极其重要：以 NULL 结尾

        pl.count++;
        pipe_line = strtok_r(NULL, "|", &saveptr);
    }

    int pre_p = -1; // 接收上一个管道的读端
    pid_t pids[MAX_CMDS]; // 保存所有子进程 PID，用于后续统一等待

    for(int i = 0; i < pl.count; i++) {
        command_t *cmd = &pl.commands[i];

        // 处理内置命令
        if(strcmp(cmd->argv[0], "exit") == 0 || strcmp(cmd->argv[0], "cd") == 0) {
            if (pl.count > 1) {
                // 如果内置命令出现在管道中，则拒绝执行
                fprintf(stderr, "Builtins in pipeline are not allowed\n");
                return 0;
            } else {
                if(strcmp(cmd->argv[0], "exit") == 0) return 1;
                if(strcmp(cmd->argv[0], "cd") == 0) {
                    if(cmd->argc < 2) {
                        // 简单处理：没有参数就回 HOME，如果失败打印错误
                        char *home = getenv("HOME");
                        if (home && chdir(home) != 0) perror("cd");
                    } else {
                        if(chdir(cmd->argv[1]) != 0) perror("cd");
                    }
                    return 0; 
                }
            }
        }

        int fd[2]; 
        // 只有不是最后一个命令，才需要创建管道传给下一个
        if (i < pl.count - 1) {
            if (pipe(fd) < 0) {
                perror("pipe");
                return 0;
            }
        }

        pids[i] = fork();
        if(pids[i] < 0) {
            perror("fork");
            return 0;
        } else if(pids[i] == 0) {
         
            if(pre_p != -1) {  // 接收上一个命令的输出
                dup2(pre_p, STDIN_FILENO); 
                close(pre_p); 
            }
            if(i < pl.count - 1) { // 如果不是最后一个命令，把输出送进管道
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]); 
                close(fd[0]); 
            }
            
            execvp(cmd->argv[0], cmd->argv);
            perror(cmd->argv[0]); // 如果 execvp 返回了，说明找不到命令
            _exit(1); // 必须调用 _exit
        } else {
            // --- 父进程 ---
            if(pre_p != -1) {
                close(pre_p); // 关闭上一个命令的读端，防止死锁
            }
            if(i < pl.count - 1) {
                close(fd[1]); // 父进程不需要写端，必须关闭
                pre_p = fd[0]; // 将当前的读端留给下一次循环的命令
            }
        }
    }

    for (int i = 0; i < pl.count; i++) {
        
        waitpid(pids[i], NULL, 0);
    }

    return 0;
}

int main(void) {
    char *line = NULL;
    size_t capacity = 0;

    while (1) {
        print_prompt();
        if (!read_line(&line, &capacity)) {
            putchar('\n');
            break;
        }
        if (handle_line(line)) {
            break;
        }
    }

    free(line);
    return 0;
}

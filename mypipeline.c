#define _POSIX_C_SOURCE 200112L
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    int pipefd[2]; //create pipe (1)
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "(parent_process>forking…)\n");
    pid_t pid1 = fork();
    if (pid1 < 0) { perror("fork"); exit(EXIT_FAILURE); }

// child 1  (3)
    if (pid1 == 0) {                         
        fprintf(stderr,
                "(child1>redirecting stdout to the write end of the pipe…)\n");
        close(STDOUT_FILENO);
        dup(pipefd[1]);                       
        close(pipefd[1]);
        close(pipefd[0]);

        fprintf(stderr, "(child1>going to execute cmd: ls -ls)\n");
        char *args[] = {"ls", "-ls", NULL};
        execvp("ls", args);
        perror("execvp ls");                  
        _exit(EXIT_FAILURE);
    }
//parent (4)
    fprintf(stderr, "(parent_process>created process with id: %d)\n", pid1);
    fprintf(stderr, "(parent_process>closing the write end of the pipe…)\n");
    close(pipefd[1]);                      

    fprintf(stderr, "(parent_process>forking…)\n");
    pid_t pid2 = fork();
    if (pid2 < 0) { perror("fork"); exit(EXIT_FAILURE); }
//child2
    if (pid2 == 0) {                        
        fprintf(stderr,
                "(child2>redirecting stdin to the read end of the pipe…)\n");
        close(STDIN_FILENO);
        dup(pipefd[0]);                       
        close(pipefd[0]);

        fprintf(stderr, "(child2>going to execute cmd: wc)\n");
        char *args[] = {"wc", NULL};
        execvp("wc", args);
        perror("execvp wc");
        _exit(EXIT_FAILURE);
    }

//parent
    fprintf(stderr, "(parent_process>created process with id: %d)\n", pid2);

    fprintf(stderr, "(parent_process>closing the read end of the pipe…)\n");
    close(pipefd[0]);                         

    fprintf(stderr,
            "(parent_process>waiting for child processes to terminate…)\n");
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    fprintf(stderr, "(parent_process>exiting…)\n");
    return 0;
}

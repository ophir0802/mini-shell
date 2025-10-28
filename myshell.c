// myshell.c – readable style (expanded conditionals, braces, spacing)
//  "/* === FIX ... === */" markers kept.  All logic unchanged.
/* Need to set _POSIX_C_SOURCE to use some functions */
#define _POSIX_C_SOURCE 200809L  // Updated to 200809L for strdup support

#include <stdio.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>  // Include for string functions
#include <strings.h> // Also include for strdup on some systems
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include "LineParser.h"


#define TERMINATED  -1
#define RUNNING      1
#define SUSPENDED    0

typedef struct process {
    cmdLine           *cmd;    /* duplicate of original line */
    pid_t              pid;
    int                status;
    struct process    *next;
} process;

//globals
static int debug = 0;
static process *processList = NULL;

static void execute(cmdLine *pCmdLine);
static void addProcess(process **process_list, cmdLine *cmd, pid_t pid);
static void printProcessList(process **process_list);
static void updateProcessList(process **process_list);
static void updateProcessStatus(process *process_list, int pid, int status);
static void freeProcessList(process *process_list);


static cmdLine *dupCmdLine(const cmdLine *src){
    if (src == NULL) {
        return NULL;
    }
    cmdLine *dup = (cmdLine *)calloc(1, sizeof(cmdLine));
    if (dup == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    dup->argCount = src->argCount;
    for (int i = 0; i < src->argCount; i++) {
        if (src->arguments[i] != NULL) {
            ((char **)dup->arguments)[i] = strdup(src->arguments[i]);
        }
    }

    dup->inputRedirect  = src->inputRedirect  ? strdup(src->inputRedirect)  : NULL;
    dup->outputRedirect = src->outputRedirect ? strdup(src->outputRedirect) : NULL;
    dup->blocking       = src->blocking;
    dup->idx            = src->idx;
    dup->next           = NULL;      //avoid conection

    return dup;
}
static void freeDupCmdLine(cmdLine *c){
    if (c == NULL) {
        return;
    }

    for (int i = 0; i < c->argCount; i++) {
        free((char *)c->arguments[i]);
    }

    free((char *)c->inputRedirect);
    free((char *)c->outputRedirect);
    free(c);
}
/* === END FIX === */

/* === FIX: PID string validator === */
static int parsePid(const char *s, pid_t *out)
{
    if (s == NULL) {
        return -1;
    }

    char *end;
    long  val = strtol(s, &end, 10);

    if (end == s || *end != '\0' || val <= 0) {
        return -1;
    }

    *out = (pid_t)val;
    return 0;
}
/* === END FIX === */

/************************  main loop  ************************/ 
int main(int argc, char *argv[])
{
    /* check debug flag */
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        debug = 1;
    }

    char cwd[PATH_MAX];

    while (1) {
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd");
        }

        printf("%s$ ", cwd);
        fflush(stdout);

        char *buf = malloc(2048);
        if (buf == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        if (fgets(buf, 2048, stdin) == NULL) {
            free(buf);
            break;          /* EOF */
        }

        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[len - 1] = '\0';
        }

        if (buf[0] == '\0') {
            free(buf);
            continue;       /* empty line */
        }

        cmdLine *userCmd = parseCmdLines(buf);
        free(buf);

        if (userCmd == NULL) {
            continue;
        }

        /* built‑in quit */
        if (strcmp(userCmd->arguments[0], "quit") == 0) {
            puts("quiting");
            freeCmdLines(userCmd);
            freeProcessList(processList);
            processList = NULL;   /* === FIX: avoid UAF === */
            break;
        }

        execute(userCmd);
        freeCmdLines(userCmd);     /* original chain freed safely */
    }

    return 0;
}

//execute
static void execute(cmdLine *pCmdLine)
{
    char *const *args = pCmdLine->arguments;
    //basic commands
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL || chdir(args[1]) != 0) {
            perror("cd");
        }
        return;
    }

    pid_t targ;

    if (strcmp(args[0], "halt") == 0) {
        if (parsePid(args[1], &targ) == -1) {
            fprintf(stderr, "halt: invalid pid\n");
            return;
        }
        if (kill(targ, SIGSTOP) == -1) {
            perror("halt");
        }
        return;
    }

    if (strcmp(args[0], "wakeup") == 0) {
        if (parsePid(args[1], &targ) == -1) {
            fprintf(stderr, "wakeup: invalid pid\n");
            return;
        }
        if (kill(targ, SIGCONT) == -1) {
            perror("wakeup");
        }
        return;
    }

    if (strcmp(args[0], "ice") == 0) {
        if (parsePid(args[1], &targ) == -1) {
            fprintf(stderr, "ice: invalid pid\n");
            return;
        }
        if (kill(targ, SIGINT) == -1) {
            perror("ice");
        }
        return;
    }

    if (strcmp(args[0], "procs") == 0) {
        printProcessList(&processList);
        return;
    }

    //Using Fork
    int pipefd[2] = { -1, -1 };

    //case chain
    if (pCmdLine->next != NULL) {
        if (pCmdLine->outputRedirect != NULL || pCmdLine->next->inputRedirect != NULL) { //case I/O modification with chain
            fprintf(stderr, "illegal pipe redirection\n");
            return;
        }

        if (pipe(pipefd) == -1) {
            perror("pipe");
            return;
        }

        pid_t pidPre = fork();
        if (pidPre < 0) {
            perror("fork");
            close(pipefd[0]);
            close(pipefd[1]);   
            return;
        }

        if (pidPre == 0) { //left child handle    
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);

            execvp(args[0], args);  
            perror("exec (left pipe)");
            _exit(EXIT_FAILURE);
        }
        else{
            addProcess(&processList, pCmdLine, pidPre); /* store duplicate cmdLine */
            if (pCmdLine->blocking) {
                waitpid(pidPre, NULL, 0);
            }
            close(pipefd[1]);          /* parent closes write end */
            pCmdLine = pCmdLine->next; /* continue with right‑side cmd */
            args = pCmdLine->arguments;

        }
    }
    
    pid_t pid = fork();
    //error
    if (pid < 0) {
        perror("fork");
        if (pipefd[0] != -1) {
            close(pipefd[0]);
        }
        return;
    }
    
    if (pid > 0) { //parent
        if (debug) {
            fprintf(stderr, "PID: %d\n", pid);
            fprintf(stderr, "Executing: %s\n", args[0]);
        }

        if (pipefd[0] != -1) {
            close(pipefd[0]);
        }
        addProcess(&processList, pCmdLine, pid);
        if (pCmdLine->blocking) {
            waitpid(pid, NULL, 0);
        }
    }
    else { //child (or child2)
        if (pipefd[0] != -1) {
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
        }
        if (pCmdLine->inputRedirect != NULL) { //input change
            FILE *in = fopen(pCmdLine->inputRedirect, "r");
            if (in == NULL) {
                perror("open input");
                _exit(EXIT_FAILURE);
            }
            dup2(fileno(in), STDIN_FILENO);
            fclose(in);
        }
        if (pCmdLine->outputRedirect != NULL) { //output change 
            FILE *out = fopen(pCmdLine->outputRedirect, "w");
            if (out == NULL) {
                perror("open output");
                _exit(EXIT_FAILURE);
            }
            dup2(fileno(out), STDOUT_FILENO);
            fclose(out);
        }
        execvp(args[0], args);  
        perror("execvp");
        _exit(EXIT_FAILURE);
    }
}

/***************** process‑list helpers *******************/
static const char *statusStr(int s)
{
    if (s == RUNNING) {
        return "RUNNING";
    }
    if (s == SUSPENDED) {
        return "SUSPENDED";
    }
    if (s == TERMINATED) {
        return "TERMINATED";
    }
    return "UNKNOWN";
}

static void addProcess(process **process_list, cmdLine *original, pid_t pid)
{
    process *np = (process *)malloc(sizeof(process));
    if (np == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    np->cmd    = dupCmdLine(original);   /* === FIX: store duplicate === */
    np->pid    = pid;
    np->status = RUNNING;
    np->next   = *process_list;
    *process_list = np;
}

static void updateProcessStatus(process *list, int pid, int status)
{
    while (list != NULL) {
        if (list->pid == pid) {
            list->status = status;
            return;
        }
        list = list->next;
    }
}
static void updateProcessList(process **plist)
{
    if (plist == NULL || *plist == NULL) {
        return;
    }

    int      st;
    process *p = *plist;
    while (p != NULL) {
        pid_t res = waitpid(p->pid, &st, WNOHANG | WUNTRACED | WCONTINUED);  // WCONTINUED used instead of __W_CONTINUED
        if (res == 0) {
            /* no change */
        }
        else if (res == -1) {
            updateProcessStatus(*plist, p->pid, TERMINATED);
        }
        else {
            if (WIFEXITED(st) || WIFSIGNALED(st)) {
                updateProcessStatus(*plist, p->pid, TERMINATED);
            }
            else if (WIFSTOPPED(st)) {
                updateProcessStatus(*plist, p->pid, SUSPENDED);
            }
            else if (WIFCONTINUED(st)) {  // Now properly defined with _POSIX_C_SOURCE 200809L
                updateProcessStatus(*plist, p->pid, RUNNING);
            }
        }
        p = p->next;
    }
}

static void printProcessList(process **plist)
{
    if (plist == NULL || *plist == NULL) {
        return;
    }

    updateProcessList(plist);

    puts("PID      Command              STATUS");

    for (process *cur = *plist; cur != NULL; cur = cur->next) {
        printf("%-8d %-20s %s\n", cur->pid,
               cur->cmd->arguments[0],
               statusStr(cur->status));
    }

    /* delete freshly terminated nodes */
    process *curr = *plist;
    process *prev = NULL;

    while (curr != NULL) {
        if (curr->status == TERMINATED) {
            process *dead = curr;

            if (prev != NULL) {
                prev->next = curr->next;
            }
            else {
                *plist = curr->next;
            }

            curr = curr->next;
            freeDupCmdLine(dead->cmd);
            free(dead);
            continue;
        }

        prev = curr;
        curr = curr->next;
    }
}

static void freeProcessList(process *list)
{
    while (list != NULL) {
        process *next = list->next;
        freeDupCmdLine(list->cmd);
        free(list);
        list = next;
    }
}
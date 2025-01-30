#define _GNU_SOURCE
#include <stdio.h>

#include<signal.h>
#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>
#include<linux/unistd.h>
#include<linux/sched.h>
#include<sched.h>
#include<sys/wait.h>
#include<sys/syscall.h>

#include "common/logger.h"
#include "process/process.h"
#include "process/parser.h"
#include "process/isoproc.h"
#include "process/helper.h"


void start_process(char* process_yaml_loc, struct Process* p) {

    struct LogContext ctx;
    get_std_logger(&ctx);   

    log_info(&ctx, "starting process");

    parse_process_yaml(process_yaml_loc, p);
    
    if ( chdir(p->ContextDir) != 0 ) {
        graceful_exit(p, "error chdir", 1);
    }

    int clone_flags = SIGCHLD | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS ;
    char* cmd_stack = malloc(STACKSIZE);

    if( chdir(p->ContextDir) != 0 ) {
        log_error(&ctx, "error chdir\n");
        graceful_exit(p, "error chdir to context directory\n" ,1);
    }


    if( pipe(p->fd) < 0 || pipe(p->stdin_fd) < 0 || pipe(p->stdout_fd) || pipe(p->stderr_fd) ) {
        log_error(&ctx, "error pipe\n");
        graceful_exit(p, "error pipe\n", 1);
    }

    pid_t pid = clone(isoproc, cmd_stack + STACKSIZE, clone_flags, (void*)p);
    if (pid == -1){
        perror("clone");
        free(cmd_stack);
        exit(EXIT_FAILURE);
    }

    // parent still 
    p->Pid = pid;

    close(p->stdin_fd[0]);
    close(p->stderr_fd[1]);
    close(p->stdout_fd[1]);

    char buf[2];
    // wait for the mntfs to succeed
    if( read(p->fd[0], buf, 2) != 2 ) {
        log_error(&ctx, "error reading from fd\n");
        graceful_exit(p, "error reading pipe\n", 1);
    }

    prepare_userns(p);
    p->Pid = pid;
    p->Stack = cmd_stack;   

    if( write(p->fd[1], "OK", 2) != 2 ) {
        log_error(&ctx, "error writing to pipe\n");
        graceful_exit(p, "error writing to pipe\n", 1);
    }

    if( waitpid(pid, NULL, 0) == -1 ) {
        graceful_exit(p, "waitpid failed\n", 1);
    }

    graceful_exit(p, "success\n", 0);
}


void print_usage() {
    printf("\nUsage: sudo ./procmand <filepath>\n");
}


int main(int argc, char* argv[]) {
    struct LogContext ctx;
    get_std_logger(&ctx);    
    if (argc < 2) {
        print_usage();
        return 1;
    }

    // to unblock the daemon from waiting for one process
    pid_t pid = fork();
    if( pid < 0 ) {
        perror("error fork");
        exit(1);
    } else if( pid == 0 ) {
        log_info(&ctx, "child: process started\n");
        struct Process* p = calloc(1, sizeof(struct Process));
        start_process(argv[1], p);
        free_process(p);
        log_info(&ctx, "child: process finished\n");
    } else {
        log_info(&ctx, "parent: waiting for child\n");
        int status;
        while(1) {
            sleep(1);
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                log_info(&ctx, "child executed successfully\n");
                break;
            } else if (WIFSIGNALED(status)) {
                log_error(&ctx, "child terminated with signal\n");
                break;
            }         
        } 
    }

    return 0;
}
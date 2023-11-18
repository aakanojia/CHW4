#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include "helper.h"
#include "debug.h"
#include "deet.h"
#include "deet_run.h"

// Global flag for SIGCHLD signal
volatile sig_atomic_t sigchld_received = 0;

ProcessInfo process_table[MAX_PROCESSES];
int process_count = 0;

int get_deet_id(pid_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].pid == pid) {
            return process_table[i].deet_id;
        }
    }
    return -1; // PID not found
}

const char* get_command_line(pid_t pid) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].pid == pid) {
            return process_table[i].command_line;
        }
    }
    return ""; // PID not found
}

void sigint_handler(int sig) {
    const char msg[] = "Caught SIGINT!\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    log_shutdown();
}

void handle_sigchld() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < process_count; i++) {
            if (process_table[i].pid == pid) {
                if (WIFSTOPPED(status)) {
                    process_table[i].state = PSTATE_STOPPED;
                    log_state_change(pid, PSTATE_RUNNING, PSTATE_STOPPED, WSTOPSIG(status));
                    //write("%d\t%d\tT\tstopped\t\t%s\n", process_table[i].deet_id, pid, process_table[i].command_line);
                } else if (WIFCONTINUED(status)) {
                    process_table[i].state = PSTATE_RUNNING;
                    log_state_change(pid, PSTATE_STOPPED, PSTATE_RUNNING, 0);
                } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    process_table[i].state = PSTATE_DEAD;
                    log_state_change(pid, PSTATE_RUNNING, PSTATE_DEAD, WTERMSIG(status));
                    // Remove the process from the process table
                    // ... (code to remove process from process_table)
                }
                break;
            }
        }
    }

    // Reset the flag
    sigchld_received = 0;

    // Re-display the prompt
    // printf("deet> yo");
    // fflush(stdout);
}

void sigchld_handler(int sig) {
    sigchld_received = 1;

    // Log the signal reception
    log_signal(sig);

    // Temporarily unblock SIGCHLD for waitpid
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &mask, &oldmask);

    // Handle SIGCHLD immediately
    handle_sigchld();

    // Restore the old signal mask
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

void update_process_state(pid_t pid, PSTATE new_state) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].pid == pid) {
            process_table[i].state = new_state;
            break;
        }
    }
}

pid_t get_pid(int deet_id) {
    for (int i = 0; i < process_count; i++) {
        if (process_table[i].deet_id == deet_id) {
            return process_table[i].pid;
        }
    }
    return -1; // Deet ID not found
}
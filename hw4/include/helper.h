#ifndef HELPER_H
#define HELPER_H

#include "deet.h"

#define MAX_PROCESSES 128

extern volatile sig_atomic_t sigchld_received;

typedef struct {
    pid_t pid; // Process ID
    int deet_id; // Deet ID
    char command_line[256]; // Command line
    PSTATE state; // Process state using PSTATE enum
    bool traced; // Indicates if the process is being traced
} ProcessInfo;

extern ProcessInfo process_table[MAX_PROCESSES];
extern int process_count;

int get_deet_id(pid_t pid);

const char* get_command_line(pid_t pid);

void sigint_handler(int sig);

void handle_sigchld();

void sigchld_handler(int sig);

void update_process_state(pid_t pid, PSTATE new_state);

pid_t get_pid(int deet_id);

#endif
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include "debug.h"
#include "deet.h"
#include "deet_run.h"

#define MAX_PROCESSES 128

// Global flag for SIGCHLD signal
volatile sig_atomic_t sigchld_received = 0;

typedef struct {
    pid_t pid; // Process ID
    int deet_id; // Deet ID
    char command_line[256]; // Command line
} ProcessInfo;

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

void sigchld_handler(int sig) {
    sigchld_received = 1;
    printf("DEBUG: SIGCHLD handler called with signal %d\n", sig); // Debug print
    log_signal(sig); // Log the signal reception

    // Handle SIGCHLD signal
    int saved_errno = errno;
    int status;
    pid_t pid;

    while ((pid = waitpid((pid_t)(-1), &status, WNOHANG)) > 0) {
        if (WIFSTOPPED(status)) {
            log_state_change(pid, PSTATE_STOPPING, PSTATE_STOPPED, WSTOPSIG(status));
        }
        if (WIFCONTINUED(status)) {
            log_state_change(pid, PSTATE_CONTINUING, PSTATE_RUNNING, 0);
        }
    }

    errno = saved_errno;
}


void handle_sigchld() {
    // Iterate over process_table and print stopped processes
    for (int i = 0; i < process_count; i++) {
        int status;
        pid_t result = waitpid(process_table[i].pid, &status, WNOHANG);
        if (result > 0 && WIFSTOPPED(status)) {
            // Process is stopped
            printf("%d\t%d\tT\tstopped\t\t%s\n", process_table[i].deet_id, process_table[i].pid, process_table[i].command_line);
        }
    }

    // Reset the flag
    sigchld_received = 0;

    // Re-display the prompt
    printf("deet> ");
    fflush(stdout);
}

void run_deet() {
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0; // or SA_RESTART
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa_chld.sa_mask);

    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    char input[256];
    log_startup(); // Log startup

    // Counter for number of processes
    static int deet_id_counter = 0;

    while (1) {
        if (sigchld_received) {
            handle_sigchld();
        }

        log_prompt(); // Log prompt
        printf("deet> ");

        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (feof(stdin)) {
                // End of file reached, handle as needed, maybe exit
                printf("\nEnd of input, exiting.\n");
                break;
            } else {
                // Input error, handle or report error
                perror("fgets");
                continue;
            }
        }

        // Remove newline character from input
        input[strcspn(input, "\n")] = 0;

        // Parse the input into command and arguments
        char *command = strtok(input, " ");
        if (command == NULL) {
            log_error("Invalid command");
            printf("?\n");
            continue;
        }

        char *args[10];
        int i = 0;
        while (i < 10 && (args[i] = strtok(NULL, " ")) != NULL) i++;

        // Concatenate command line arguments
        char command_line[256] = {0};
        for (int j = 0; args[j] != NULL; j++) {
            strcat(command_line, args[j]);
            if (args[j + 1] != NULL) strcat(command_line, " ");
        }

        // Execute commands
        if (strcmp(command, "help") == 0) {
            // Display help information
            log_input("help\n"); // Log the help command
            printf("Available commands:\n");
            printf("help -- Print this help message\n");
            printf("quit (<=0 args) -- Quit the program\n");
            printf("show (<=1 args) -- Show process info\n");
            printf("run (>=1 args) -- Start a process\n");
            printf("stop (1 args) -- Stop a running process\n");
            printf("cont (1 args) -- Continue a stopped process\n");
            printf("release (1 args) -- Stop tracing a process, allowing it to continue normally\n");
            printf("wait (1-2 args) -- Wait for a process to enter a specified state or terminate\n");
            printf("kill (1 args) -- Forcibly terminate a process\n");
            printf("peek (2-3 args) -- Read from the address space of a traced process\n");
            printf("poke (3 args) -- Write to the address space of a traced process\n");
            printf("bt (1 args) -- Show a stack trace for a traced process\n");
        } else if (strcmp(command, "quit") == 0) {
            log_input("quit\n"); // Log the quit command
            log_shutdown(); // Log shutdown
            break;
        } else if (strcmp(command, "show") == 0) {
            // Show process info
        } else if (strcmp(command, "run") == 0) {
            log_input(command_line);
            printf("\n");

            // Start a process
            pid_t pid = fork();

            if (pid == -1) {
                perror("fork");
                // Handle fork error
            } else if (pid == 0) {
                // Child process
                execvp(args[0], args);
                perror("execvp"); // execvp only returns on error
                exit(EXIT_FAILURE);
            } else if (pid > 0) {
                // Parent process
                int deet_id = deet_id_counter++;
                if (process_count < MAX_PROCESSES) {
                    process_table[process_count].pid = pid;
                    process_table[process_count].deet_id = deet_id;
                    strncpy(process_table[process_count].command_line, command_line, sizeof(process_table[process_count].command_line));
                    process_count++;
                } else {
                    // Handle error: too many processes
                }
                log_state_change(pid, PSTATE_NONE, PSTATE_RUNNING, 0); // Log state change to running


                // Display process information
                printf("%d\t%d\tT\t%s\t\t%s\n", deet_id, pid, "running", command_line);

                // Stop the child process immediately
                kill(pid, SIGSTOP);
                log_state_change(pid, PSTATE_RUNNING, PSTATE_STOPPING, 0);

                // Display process information again after stopping
                //printf("%d\t%d\tT\t%s\t\t%s\n", deet_id, pid, "stopped", command_line);
            }
        } else if (strcmp(command, "stop") == 0) {
            // Stop a running process
        } else if (strcmp(command, "cont") == 0) {
            // Continue a stopped process
            if (args[0] == NULL) {
                printf("No PID provided\n");
                continue;
            }

            // Extract the PID from the command arguments
            pid_t pid_to_continue = atoi(args[0]); // Extract PID from args

            // Send SIGCONT to the specified process
            kill(pid_to_continue, SIGCONT);
            log_state_change(pid_to_continue, PSTATE_STOPPED, PSTATE_RUNNING, 0);
        } else if (strcmp(command, "release") == 0) {
            // Stop tracing a process
        } else if (strcmp(command, "wait") == 0) {
            // Wait for a process
        } else if (strcmp(command, "kill") == 0) {
            // Terminate a process
        } else if (strcmp(command, "peek") == 0) {
            // Read from address space
        } else if (strcmp(command, "poke") == 0) {
            // Write to address space
        } else if (strcmp(command, "bt") == 0) {
            // Show a stack trace
        } else {
            log_error("Invalid command");
            printf("?\n");
        }
    }
}
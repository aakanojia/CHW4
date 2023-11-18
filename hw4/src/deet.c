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

void run_deet(int silent_logging) {
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0; // or SA_RESTART
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGCHLD); // Block SIGCHLD while handling SIGINT

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa_chld.sa_mask);

    // Block other signals during SIGCHLD handling
    sigaddset(&sa_chld.sa_mask, SIGINT);
    sigaddset(&sa_chld.sa_mask, SIGTERM);
    // Add other signals

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

            // Iterate through the process table and terminate each process
            // for (int i = 0; i < process_count; i++) {
            //     if (process_table[i].state != PSTATE_DEAD) {
            //         kill(process_table[i].pid, SIGTERM); // Send SIGTERM to each process
            //         waitpid(process_table[i].pid, NULL, 0); // Wait for the process to terminate
            //     }
            // }

            log_shutdown(); // Log shutdown
            break;
        } else if (strcmp(command, "show") == 0) {
            log_input(command_line);
            if (silent_logging == 0) {
                printf("\n"); // Only print newline if logging is not silent
            }
            // Show process info
            int specific_deet_id = -1; // Default to -1, indicating no specific deet ID provided
                if (args[0] != NULL) {
                    specific_deet_id = atoi(args[0]); // Convert argument to integer
                }

                int found = 0; // Flag to check if any process is found
                for (int i = 0; i < process_count; i++) {
                        if (specific_deet_id == -1 || process_table[i].deet_id == specific_deet_id) {
                            char state_char = process_table[i].traced ? 'T' : 'U';
                            char *state_desc = "unknown";

                            switch (process_table[i].state) {
                                case PSTATE_RUNNING:
                                    state_desc = "running";
                                    break;
                                case PSTATE_STOPPED:
                                    state_desc = "stopped";
                                    break;
                                case PSTATE_DEAD:
                                    state_desc = "dead";
                                    break;
                                case PSTATE_NONE:
                                    state_desc = "none";
                                    break;
                                case PSTATE_STOPPING:
                                    state_desc = "stopped";
                                    break;
                                case PSTATE_CONTINUING:
                                    state_desc = "continuing";
                                    break;
                                case PSTATE_KILLED:
                                    state_desc = "killed";
                                    break;
                                default:
                                    state_desc = "unknown";
                            }

                            printf("%d\t%d\t%c\t%s\t\t%s\n",
                                   process_table[i].deet_id, process_table[i].pid, state_char, state_desc, process_table[i].command_line);
                            found = 1;
                            if (specific_deet_id != -1) break;
                        }
                    }
                if (!found) {
                    if (specific_deet_id == -1) {
                        printf("No processes are currently being managed.\n");
                    } else {
                        printf("No process found with Deet ID: %d\n", specific_deet_id);
                    }
                }
        } else if (strcmp(command, "run") == 0) {
            log_input(command_line);
            if (silent_logging == 0) {
                printf("\n"); // Only print newline if logging is not silent
            }

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
                    process_table[process_count].traced = true;  // Set traced flag to true
                    process_table[process_count].state = PSTATE_RUNNING; // Initially stopped due to SIGSTOP
                    process_count++;
                } else {
                    // Handle error: too many processes
                }
                log_state_change(pid, PSTATE_NONE, PSTATE_RUNNING, 0); // Log state change to running

                // Display process information
                printf("%d\t%d\tT\t%s\t\t%s\n", deet_id, pid, "running", command_line);

                // Stop the child process immediately
                log_signal(SIGCHLD);
                log_state_change(pid, PSTATE_RUNNING, PSTATE_STOPPED, 0);
                printf("%d\t%d\tT\t%s\t\t%s\n", deet_id, pid, "stopped", command_line);
                process_table[process_count - 1].state = PSTATE_STOPPED; // Initially stopped due to SIGSTOP
                kill(pid, SIGSTOP);

                // Display process information again after stopping
                //printf("%d\t%d\tT\t%s\t\t%s\n", deet_id, pid, "stopped", command_line);
            }
        } else if (strcmp(command, "stop") == 0) {
            // Stop a running process
        } else if (strcmp(command, "cont") == 0) {
            log_input(command_line);
            // Continue a stopped process
            if (args[0] == NULL) {
                printf("No Deet ID provided\n");
                continue;
            }

            // Extract the PID from the command arguments
            int deet_id_to_continue = atoi(args[0]); // Extract Deet ID from args
            pid_t pid_to_continue = get_pid(deet_id_to_continue); // Convert Deet ID to PID

            if (pid_to_continue == -1) {
                printf("Invalid Deet ID: %d\n", deet_id_to_continue);
                continue;
            }

            // Send SIGCONT to the specified process
            kill(pid_to_continue, SIGCONT);
            log_state_change(pid_to_continue, PSTATE_STOPPED, PSTATE_RUNNING, 0);

            // Update the process state in the process table
            update_process_state(pid_to_continue, PSTATE_RUNNING);
        } else if (strcmp(command, "release") == 0) {
            // Stop tracing a process
        } else if (strcmp(command, "wait") == 0) {
            // Wait for a process
        } else if (strcmp(command, "kill") == 0) {
            log_input(command_line);
            // Terminate a process
            if (args[0] == NULL) {
                printf("No PID provided\n");
                continue;
            }

            pid_t pid_to_kill = atoi(args[0]); // Extract PID from args

            if (kill(pid_to_kill, SIGTERM) == -1) {
                perror("kill");
            } else {
                log_state_change(pid_to_kill, PSTATE_RUNNING, PSTATE_KILLED, 0);
            }
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

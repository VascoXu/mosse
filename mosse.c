#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include "mosse.h"

// Signal handler for CTRL-C
void sigint_handler(int sig)
{
	printf("\nEnding because of ctrl-c, flushing stdout\n");
	fflush(stdout);
	close(1);
	exit(0);
	return;
}

// Display usage of program to user
void usage()
{
    printf(
        "Usage:\r\n"
        "\t[-e <events>]                    : event selector. use 'mosse list' to list events\r\n"
        "\t[-o <output file>]               : output file name\r\n"
        "\r\n"
    );
}

void list()
{
    printf("List\n");
}

char **parse_csv(char *events[], int num_events)
{
    FILE *fp = fopen("armv7_events.csv", "r");
    char **mappings = malloc(sizeof(char *) * num_events);
    char buf[1024];
    char *row;

    if (!fp) {
        printf("Error: could not open CSV file\n");
        return 0;
    }

    int map = 0;
    fgets(buf, 1024, fp);
    while (fgets(buf, 1024, fp)) {
        row = buf;
        char *event_name = strsep(&row, ",");
        char *event_code = strsep(&row, ",");
        if (event_name) {
            int i;
            for (i = 0; i < num_events; i++) {
                if (strcmp(event_name, events[i]) == 0) {
                    char *code = strdup(event_code);
                    code[strcspn(code, "\r\n")] = 0;
                    mappings[map++] = code;
                    break;
                }
            }
        }
    }
    fclose(fp);
    return mappings;
}

int main(int argc, char *argv[])
{
    // Check for usage command
    if (strcmp(argv[1], "usage") == 0) {
        usage();
        return 0;
    }

    // Check for list command
    if (strcmp(argv[1], "list") == 0) {
        list();
        return 0;
    }
    
    // Set task to run on specific core
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    int res = sched_setaffinity(0, sizeof(mask), &mask);
    if (res != 0) {
        fprintf(stderr, "Failed to set affinity\n");
    }

    // Parse cmd-line arguments
    char *events = NULL;
    char *foldername = NULL;
    int opt;
    while ((opt = getopt(argc, argv, ":e:o:")) != -1) {
    switch (opt) {
        case 'e':
            // Select events to monitor
            events = optarg;
            break;
        case 'o':
            // Output folder name
            foldername = optarg;
            break;
        case '?':
            // Unknown option
            printf("Unknown option: %c\n", optopt);
            break;
        }
    }

    // Ensure events are provided
    if (events == NULL) {
        fprintf(stderr, "Error: no events provided\n");
        return 0;
    }

    // Parse events
    char *mosse_events[MAX_COUNTERS];
    char *event;
    int num_events = 0;
    event = strsep(&events, ",");
    mosse_events[num_events] = event;
    while (event != NULL) {
        num_events++;
        event = strsep(&events, ",");
        mosse_events[num_events] = event;
    }

    printf("num_events: %d\n", num_events);
    
    // Check for cycles
    int i;
    int is_cycles = 0;
    int cycles_index = 0;
    for (i = 0; i < num_events; i++) {
        if (strcmp(mosse_events[i], "cycles") == 0) {
            is_cycles = 1;
            cycles_index = i;
        }
    }
    
    // Parse CSV file for corresponding events
    char **mappings = parse_csv(mosse_events, num_events);

    // Read program name from user input
    char *benchmark = argv[argc-1];

    // Create new directory
    // TODO: default foldername
    struct stat st = {0};
    if (foldername == NULL) {
        fprintf(stderr, "Error: no folder name provided\n");
        return 0;
    }
    char directory[strlen(foldername) + 2];
    strcpy(directory, foldername);
    strcat(directory, "/");
    if (stat(directory, &st) == -1) {
        mkdir(directory, 0700);
    }
    
    // Create output files
    FILE *output_files[num_events];
    char *output_filenames[num_events];
    for (i = 0; i < num_events; i++) {
        // Create file
        char outfile[strlen(mosse_events[i]) + strlen(directory) + 5];
        strcpy(outfile, directory);
        strcat(outfile, mosse_events[i]);
        strcat(outfile, ".txt");

        // Store file
        printf("output_file %s\n", outfile);
        output_filenames[i] = outfile; 
        output_files[i] = fopen(outfile, "w+"); 
    }

    // Setup a signal handler
    signal(SIGINT, sigint_handler);
    
    // Print base
    printf("Tracking counters of: %s\n", benchmark);

    // Read output filename from user input
    char *outfile = argv[2];

    // Calculate time 
    clock_t begin = clock();

    // Create a child process
    int pid = fork();

    int status;

    if (pid == 0) {
        char mypid[getpid()];
        sprintf(mypid, "%d", getpid());

        // Write to /proc file to set flag
        char *procs[] = {"/mosse_c1\"", "/mosse_c2\"", "/mosse_c3\"", "/mosse_c4\""};
        int iscycles = is_cycles;
        int cmds = 0;
        for (i = 0; i < num_events; i++) {
            char proc_cmd[200];
            char *part1 = "sudo sh -c \"echo ";
            char *part2 = " > /proc/";
            char *map = mappings[i];

            strcpy(proc_cmd, part1);
            strcat(proc_cmd, map);
            strcat(proc_cmd, part2);
            strcat(proc_cmd, mypid);
            if (iscycles && i == cycles_index) {
                strcat(proc_cmd, "/mosse_cc\"");
                iscycles = 0;
            }
            else {
                strcat(proc_cmd, procs[cmds++]);
            }
            printf("cmd: %s\n", proc_cmd);
            system(proc_cmd);
        }

        // Run task
        char *args[] = {benchmark, NULL};
        if (execv(args[0], args) == -1) {
            fprintf(stderr, "Error running mosse\n");
            exit(0);
        }
    }
    else {
        // Convert pid to string
        char mypid[pid];
        sprintf(mypid, "%d", pid);

        int interval_ms = 200;
        int interval_us = interval_ms*1000; //need this in microseconds for usleep call

        // Create array of /proc files
        char proc_filenames[num_events][PROC_LENGTH];
        char *procs[] = {"/mosse_c1", "/mosse_c2", "/mosse_c3", "/mosse_c4"};
        int iscycles = is_cycles;
        int files = 0;
        int i;
        for (i = 0; i < num_events; i++) {
            char counters_filename[PROC_LENGTH];
            char *procfs_path = "/proc/";
            strcpy(counters_filename, procfs_path);
            strcat(counters_filename, mypid);
            if (iscycles && i == cycles_index) {
                strcat(counters_filename, "/mosse_cc");
                iscycles = 0;
            }
            else {
                strcat(counters_filename, procs[files++]);
            }
            printf("counters_filename: %s\n", counters_filename);
            strncpy(proc_filenames[i], counters_filename, PROC_LENGTH);
        }
       
        // Open /proc files
        FILE *counter_files[num_events];
        for (i = 0; i < num_events; i++) {
            printf("filename: %s\n", proc_filenames[i]);
            FILE *counter_file = fopen(proc_filenames[i], "r");
            if (!counter_file) {
                printf("Error: could not open /proc file: %s\n", proc_filenames[i]);
            }
            counter_files[i] = counter_file;
        }

        // Read indefinitely from /proc, until CTRL-C
        iscycles = is_cycles;
        while (1) {
            if (waitpid(pid, &status, WNOHANG) != 0) {
                // End time 
                clock_t end = clock();
               
                // Calculate time spent
                double time_spent = (double) (end - begin) / CLOCKS_PER_SEC;

                // Benchmark finished running
                char *mosse_procs[] = {"/proc/mosse_c1", "/proc/mosse_c2", "/proc/mosse_c3", "/proc/mosse_c4"};
                
                // Read intermediate values
                int events = 0;
                for (i = 0; i < num_events; i++) {
                    FILE *proc_file = NULL;

                    // Check for cycles 
                    if (iscycles && i == cycles_index) {
                        proc_file = fopen("/proc/mosse_cc", "r");
                    }
                    else {
                        proc_file = fopen(mosse_procs[events++], "r");
                    }

                    char mosse_buf[2200]; // 22 (max length of counter value) * 100 (buffer size)
                    int size = fread(&mosse_buf , 1, sizeof(mosse_buf), proc_file);
                    mosse_buf[size] = '\0';

                    // Write remaining values to file
                    fputs(mosse_buf, output_files[i]);

                    // Write time spent to file
                    fprintf(output_files[i], "%f", time_spent);
                    
                    // Close file
                    fclose(output_files[i]);

                }
                
                // Print statistics
                printf("Performance counter stats for: %s\n", benchmark);
                printf("Benchmark Time: %fs\n", time_spent);
                exit(0);
            }

            struct timespec ts;

            // Calculate sleep time
            clock_gettime(CLOCK_MONOTONIC, &ts);
            long ns = ts.tv_nsec;
            time_t sec = ts.tv_sec;
    
            int i;
            for (i = 0; i < num_events; i++) {
                // Read /proc file
                char buf[1760]; // 22 (max length of counter value) * 80 (buffer size)
                int size = fread(&buf , 1, sizeof(buf), counter_files[i]);
                buf[size] = '\0';
                if (size > 0) fputs(buf, output_files[i]);
            }

            // Close and reopen /proc file to read again
            for (i = 0; i < num_events; i++) {
                fclose(counter_files[i]);
                counter_files[i] = fopen(proc_filenames[i], "r");
            }
            //usleep(interval_us);
        }
    }
	return 0;
}

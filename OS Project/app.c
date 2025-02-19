#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#define MAX_GROUPS 30
#define MAX_MESSAGE_LENGTH 256

typedef struct {
    long mtype;
    int group;
} Message;

typedef struct {
    long mtype;
    int user;
    char mtext[MAX_MESSAGE_LENGTH];
    int violations;
    int grp_id;
    int action;
    int terminator;
} ModeratorMessage;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <test_case_number>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // Construct path to input.txt inside the testcase folder.
    char input_file[256];
    snprintf(input_file, sizeof(input_file), "testcase_%s/input.txt", argv[1]);
    
    FILE *file = fopen(input_file, "r");
    if (!file) {
        perror("Error opening input.txt");
        return EXIT_FAILURE;
    }
    
    int num_groups;
    int val_msgq_key;
    int app_msgq_key;
    int mod_msgq_key;
    int violation_threshold;
    
    if(fscanf(file, "%d %d %d %d %d", &num_groups, &val_msgq_key, &app_msgq_key, &mod_msgq_key, &violation_threshold) != 5){
        fprintf(stderr, "Error reading input.txt\n");
        fclose(file);
        return EXIT_FAILURE;
    }
    
    // Read the group file paths.
    char group_files[MAX_GROUPS][256];
    for (int i = 0; i < num_groups; i++) {
        fscanf(file, "%s", group_files[i]);
    }
    fclose(file);
    
    printf("App: Using message queue key: %d (Group ↔ App)\n", app_msgq_key);
    
    // Create (or connect to) the message queue for group termination (communication with app)
    int msgq_id = msgget(app_msgq_key, IPC_CREAT | 0666);
    if (msgq_id == -1) {
        perror("msgget failed");
        return EXIT_FAILURE;
    }
    
    // For each group, construct its full file path (prepend the testcase folder)
    pid_t pids[MAX_GROUPS];
    for (int i = 0; i < num_groups; i++) {
        char full_path[512]; // Increased size to avoid truncation
        snprintf(full_path, sizeof(full_path), "testcase_%s/%s", argv[1], group_files[i]);
        
        if ((pids[i] = fork()) == 0) {
            printf("Executing: ./group.out %s %s\n", argv[1], full_path);
            execl("./group.out", "group.out", argv[1], full_path, NULL);
            perror("execl failed");
            exit(EXIT_FAILURE);
        }
    }
    
    // Wait for group termination messages
    int terminated_groups = 0;
    Message msg;
   
    while (terminated_groups < num_groups) {
        if (msgrcv(msgq_id, &msg, sizeof(Message) - sizeof(long), 0, 0) > 0) {
            printf("All users terminated. Exiting group process %d.\n", msg.group);
            terminated_groups++;
        }
    }
    
    
    // Cleanup the app↔group message queue
    msgctl(msgq_id, IPC_RMID, NULL);
    return EXIT_SUCCESS;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_GROUPS 30
#define MAX_PATH_LENGTH 256

typedef struct {
    long mtype;
    int modifyingGroup;
    int user;
} Message;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <test_case_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int testCaseNumber = atoi(argv[1]);
    char inputFilePath[MAX_PATH_LENGTH];
    snprintf(inputFilePath, sizeof(inputFilePath), "testcase_%d/input.txt", testCaseNumber);

    FILE *inputFile = fopen(inputFilePath, "r");
    if (!inputFile) {
        perror("Failed to open input.txt");
        exit(EXIT_FAILURE);
    }

    int n, validationKey, appGroupsKey, moderatorGroupsKey, violationThreshold;
    fscanf(inputFile, "%d %d %d %d %d", &n, &validationKey, &appGroupsKey, &moderatorGroupsKey, &violationThreshold);

    char groupPaths[MAX_GROUPS][MAX_PATH_LENGTH];
    for (int i = 0; i < n; i++) {
        fscanf(inputFile, "%s", groupPaths[i]);
    }
    fclose(inputFile);

    // Create message queue for app-group communication
    int appMsgQueue = msgget(appGroupsKey, IPC_CREAT | 0666);
    if (appMsgQueue == -1) {
        perror("Failed to create message queue for app-group communication");
        exit(EXIT_FAILURE);
    }

    // Spawn group processes
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid == 0) { // Child process
            char fullGroupPath[MAX_PATH_LENGTH];
            snprintf(fullGroupPath, sizeof(fullGroupPath), "testcase_%d/%s", testCaseNumber, groupPaths[i]);
            
            char validationKeyStr[20], appGroupsKeyStr[20], moderatorGroupsKeyStr[20];
            sprintf(validationKeyStr, "%d", validationKey);
            sprintf(appGroupsKeyStr, "%d", appGroupsKey);
            sprintf(moderatorGroupsKeyStr, "%d", moderatorGroupsKey);

            execl("./groups.out", "./groups.out", fullGroupPath, validationKeyStr, appGroupsKeyStr, moderatorGroupsKeyStr, NULL);
            perror("Failed to exec groups.out");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("Failed to fork group process");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for all groups to terminate
    Message msg;
    int activeGroups = n;
    while (activeGroups > 0) {
        if (msgrcv(appMsgQueue, &msg, sizeof(msg) - sizeof(msg.mtype), 0, 0) > 0) {
            printf("All users terminated. Exiting group process %d\n", msg.modifyingGroup);
            activeGroups--;
        }
    }

    // Cleanup
    msgctl(appMsgQueue, IPC_RMID, NULL);

    return 0;
}


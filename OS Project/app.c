#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#define MAX_GROUPS 30
#define MAX_PATH_LENGTH 256
#define MAX_TEXT_SIZE 256

typedef struct {
	long mtype;
	int timestamp;
	int user;
	char mtext[MAX_TEXT_SIZE];
	int modifyingGroup;
} Message;

int appMsgQueue = -1;
pid_t child_pids[MAX_GROUPS];
int num_children = 0;
int totalUsers = 0;
int totalBannedUsers = 0;
int groupsCreated = 0;
int messagesReceived = 0;

void cleanup() {
	if (appMsgQueue != -1) {
    	msgctl(appMsgQueue, IPC_RMID, NULL);
	}
    
	for (int i = 0; i < num_children; i++) {
    	if (child_pids[i] > 0) {
        	kill(child_pids[i], SIGTERM);
        	waitpid(child_pids[i], NULL, 0);
    	}
	}
}

void signal_handler(int signum) {
	cleanup();
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
    	fprintf(stderr, "Usage: %s <test_case_number>\n", argv[0]);
    	exit(EXIT_FAILURE);
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
    
	int testCaseNumber = atoi(argv[1]);
	char inputFilePath[MAX_PATH_LENGTH];
	snprintf(inputFilePath, sizeof(inputFilePath), "testcase_%d/input.txt", testCaseNumber);

	FILE *inputFile = fopen(inputFilePath, "r");
	if (!inputFile) {
    	perror("Failed to open input.txt");
    	exit(EXIT_FAILURE);
	}

	int n, validationKey, appGroupsKey, moderatorGroupsKey, violationThreshold;
	if (fscanf(inputFile, "%d %d %d %d %d", &n, &validationKey, &appGroupsKey,
           	&moderatorGroupsKey, &violationThreshold) != 5) {
    	fprintf(stderr, "Failed to read configuration\n");
    	fclose(inputFile);
    	exit(EXIT_FAILURE);
	}

	char groupPaths[MAX_GROUPS][MAX_PATH_LENGTH];
	for (int i = 0; i < n; i++) {
    	if (fscanf(inputFile, "%s", groupPaths[i]) != 1) {
        	fprintf(stderr, "Failed to read group path %d\n", i);
        	fclose(inputFile);
        	exit(EXIT_FAILURE);
    	}
	}
	fclose(inputFile);

	appMsgQueue = msgget(appGroupsKey, IPC_CREAT | 0666);
	if (appMsgQueue == -1) {
    	perror("Failed to create app message queue");
    	exit(EXIT_FAILURE);
	}

	for (int i = 0; i < n; i++) {
    	pid_t pid = fork();
    	if (pid == 0) {
        	char fullGroupPath[MAX_PATH_LENGTH];
        	snprintf(fullGroupPath, sizeof(fullGroupPath), "testcase_%d/%s",
                	testCaseNumber, groupPaths[i]);
       	 
        	char validationKeyStr[20], appGroupsKeyStr[20], moderatorGroupsKeyStr[20];
        	sprintf(validationKeyStr, "%d", validationKey);
        	sprintf(appGroupsKeyStr, "%d", appGroupsKey);
        	sprintf(moderatorGroupsKeyStr, "%d", moderatorGroupsKey);
       	 
        	execl("./groups.out", "./groups.out", fullGroupPath, validationKeyStr,
              	appGroupsKeyStr, moderatorGroupsKeyStr, NULL);
       	 
        	perror("Failed to exec groups.out");
        	exit(EXIT_FAILURE);
    	} else if (pid < 0) {
        	perror("Failed to fork group process");
        	cleanup();
        	exit(EXIT_FAILURE);
    	} else {
        	child_pids[num_children++] = pid;
        	groupsCreated++;
    	}
	}

	Message msg;
	int activeGroups = n;

	while (activeGroups > 0) {
    	if (msgrcv(appMsgQueue, &msg, sizeof(msg) - sizeof(msg.mtype), 1, 0) > 0) {
        	totalBannedUsers += msg.user;
        	activeGroups--;
        	messagesReceived++;
    	} else if (errno == EIDRM) {
        	printf("msgrcv failed: Identifier removed\n");
        	break;
    	}
	}

	// Wait for all child processes
	for (int i = 0; i < num_children; i++) {
    	int status;
    	waitpid(child_pids[i], &status, 0);
	}

	printf("==========================================\n");
	printf("Number of groups created: %d\n", groupsCreated);
	printf("Number of users created: %d\n", totalUsers);
	printf("Number of messages received: %d\n", messagesReceived);
	printf("Number of users deleted: %d\n", totalBannedUsers);
	printf("Number of groups deleted: %d\n", groupsCreated);
	printf("==========================================\n");

	cleanup();
	return 0;
}





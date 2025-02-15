#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <libgen.h>
#include <errno.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/select.h>

#define MAX_USERS 50
#define MAX_TEXT_SIZE 256

typedef struct {
	long mtype;
	int timestamp;
	int user;
	char mtext[MAX_TEXT_SIZE];
	int modifyingGroup;
} Message;

char *trimWhitespace(char *str) {
	char *end;
	while(isspace((unsigned char)*str)) str++;
	if(*str == 0) return str;
	end = str + strlen(str) - 1;
	while(end > str && isspace((unsigned char)*end)) end--;
	*(end+1) = 0;
	return str;
}

void handleUserProcess(int pipeFd[2], const char *userFilePath) {
	close(pipeFd[0]);
	FILE *userFile = fopen(userFilePath, "r");
    
	if (!userFile) {
    	perror("Failed to open user file");
    	exit(EXIT_FAILURE);
	}

	char buffer[MAX_TEXT_SIZE];
	int timestamp;
	char message[MAX_TEXT_SIZE];
    
	while (fscanf(userFile, "%d %[^\n]", &timestamp, message) == 2) {
    	snprintf(buffer, sizeof(buffer), "%d %s", timestamp, message);
    	write(pipeFd[1], buffer, strlen(buffer) + 1);
    	usleep(1000); // Small delay to prevent overwhelming the pipe
	}
    
	fclose(userFile);
	close(pipeFd[1]);
	exit(0);
}

int main(int argc, char *argv[]) {
	if (argc != 5) {
    	fprintf(stderr, "Usage: %s <group_file> <validation_key> <app_key> <moderator_key>\n", argv[0]);
    	exit(EXIT_FAILURE);
	}

	const char *groupFilePath = argv[1];
	int validationKey = atoi(argv[2]);
	int appKey = atoi(argv[3]);
	int moderatorKey = atoi(argv[4]);

	FILE *groupFile = fopen(groupFilePath, "r");
	if (!groupFile) {
    	perror("Failed to open group file");
    	exit(EXIT_FAILURE);
	}

	int numUsers;
	if (fscanf(groupFile, "%d", &numUsers) != 1) {
    	perror("Failed to read number of users from group file");
    	fclose(groupFile);
    	exit(EXIT_FAILURE);
	}

	char userFiles[MAX_USERS][MAX_TEXT_SIZE];
	for (int i = 0; i < numUsers; i++) {
    	if (fscanf(groupFile, "%s", userFiles[i]) != 1) {
        	perror("Failed to read user file path from group file");
        	fclose(groupFile);
        	exit(EXIT_FAILURE);
    	}

    	trimWhitespace(userFiles[i]);

    	char fullUserPath[MAX_TEXT_SIZE];
    	char *testCaseDir = dirname(dirname(strdup(groupFilePath)));
    	snprintf(fullUserPath, sizeof(fullUserPath), "%s/%s", testCaseDir, userFiles[i]);
    	strcpy(userFiles[i], fullUserPath);
	}
	fclose(groupFile);

	int validationQueue = msgget(validationKey, IPC_CREAT | 0666);
	int appQueue = msgget(appKey, IPC_CREAT | 0666);
	int moderatorQueue = msgget(moderatorKey, IPC_CREAT | 0666);

	if (validationQueue == -1 || appQueue == -1 || moderatorQueue == -1) {
    	perror("Failed to create message queues");
    	exit(EXIT_FAILURE);
	}

	int groupNum = atoi(basename(strdup(groupFilePath)) + 6);
    	Message initMsg = {
    	.mtype = 30 + groupNum,  // Distinct message type for each group
    	.modifyingGroup = groupNum,
    	.user = 0
	};

    	if (msgsnd(validationQueue, &initMsg, sizeof(Message) - sizeof(long), 0) == -1) {
        	if (errno == EIDRM) {
            	printf("msgsnd failed: Identifier removed\n");
            	exit(EXIT_FAILURE);
        	}
        	perror("Failed to send init message");
        	exit(EXIT_FAILURE);
    	}

	int pipes[MAX_USERS][2];
	pid_t userPids[MAX_USERS];
	int activeUsers = numUsers;
	int bannedUsers = 0;
	int isActive[MAX_USERS] = {0};

	for (int i = 0; i < numUsers; i++) {
    	if (pipe(pipes[i]) == -1) {
        	perror("Pipe creation failed");
        	exit(EXIT_FAILURE);
    	}
  	 
    	pid_t pid = fork();
    	if (pid == 0) {
        	handleUserProcess(pipes[i], userFiles[i]);
        	exit(0);
    	} else if (pid > 0) {
        	userPids[i] = pid;
        	close(pipes[i][1]); // Close write end in parent
        	isActive[i] = 1;

      	 
        	Message userMsg = {
            	.mtype = 30 + groupNum,  // Distinct message type for each group
            	.modifyingGroup = groupNum,
            	.user = i,
            	.timestamp = 0
        	};
        	if (msgsnd(validationQueue, &userMsg, sizeof(Message) - sizeof(long), 0) == -1) {
            	if (errno == EIDRM) {
                	printf("msgsnd failed: Identifier removed\n");
                	exit(EXIT_FAILURE);
            	}
        	}
    	} else {
        	perror("Fork failed");
        	exit(EXIT_FAILURE);
    	}
	}

	char buffer[MAX_TEXT_SIZE];
	int timestamp, userId;
	char text[MAX_TEXT_SIZE];
	fd_set readfds;

   while (activeUsers > 0) {
    	FD_ZERO(&readfds);
    	int maxfd = -1;
    	for (int i = 0; i < numUsers; i++) {
        	if (isActive[i]) {
            	FD_SET(pipes[i][0], &readfds);
            	if (pipes[i][0] > maxfd) maxfd = pipes[i][0];
        	}
    	}

    	struct timeval timeout = {0, 10000};  // 10ms timeout
    	int ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);

    	if (ready > 0) {
        	for (int i = 0; i < numUsers; i++) {
            	if (isActive[i] && FD_ISSET(pipes[i][0], &readfds)) {
                	ssize_t bytesRead = read(pipes[i][0], buffer, MAX_TEXT_SIZE);
                	if (bytesRead > 0) {
                    	buffer[bytesRead] = '\0';
                    	if (sscanf(buffer, "%d %[^\n]", &timestamp, text) == 2) {
                        	userId = i;

                        	Message msg;
                        	msg.mtype = 30 + groupNum;
                        	msg.timestamp = timestamp;
                        	msg.user = userId;
                        	msg.modifyingGroup = groupNum;
                        	strncpy(msg.mtext, text, MAX_TEXT_SIZE - 1);
                        	msg.mtext[MAX_TEXT_SIZE - 1] = '\0';

                        	if (msgsnd(moderatorQueue, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
                            	perror("msgsnd to moderatorQueue failed");
                        	}

                       	printf("Sending to validation: Group=%d, User=%d, Timestamp=%d, Message=%s\n", groupNum, msg.user, msg.timestamp, msg.mtext);

                         	if (msgsnd(validationQueue, &msg, sizeof(Message) - sizeof(long), 0) == -1) {
                            	perror("msgsnd to validationQueue failed");
                            	continue;
                        	}
                    	}
                	}  else if (bytesRead == 0) {
                    	close(pipes[i][0]);
                    	isActive[i] = 0;
                    	activeUsers--;
                	}
            	}
        	}
    	}

    	Message banMsg;
    	if (msgrcv(moderatorQueue, &banMsg, sizeof(Message) - sizeof(long), 2, IPC_NOWAIT) > 0) {
        	if (isActive[banMsg.user]) {
            	close(pipes[banMsg.user][0]);
            	isActive[banMsg.user] = 0;
            	activeUsers--;
            	bannedUsers++;
        	}
    	}
	}

	Message termMsg = {
    	.mtype = 30 + groupNum,
    	.modifyingGroup = groupNum,
    	.user = bannedUsers
	};
	msgsnd(validationQueue, &termMsg, sizeof(Message) - sizeof(long), 0);

	termMsg.mtype = 1; // Reset mtype for app queue
	msgsnd(appQueue, &termMsg, sizeof(Message) - sizeof(long), 0);

	for (int i = 0; i < numUsers; i++) {
    	if (isActive[i]) {
        	close(pipes[i][0]);
    	}
	}

	return 0;
}







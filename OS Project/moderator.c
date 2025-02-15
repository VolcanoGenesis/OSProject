 #include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#define MAX_FILTERED_WORDS 50
#define MAX_TEXT_SIZE 256
#define MAX_GROUPS 30
#define MAX_USERS 50
#define MAX_WORD_LENGTH 20

typedef struct {
	long mtype;
	int timestamp;
	int user;
	char mtext[MAX_TEXT_SIZE];
	int modifyingGroup;
} Message;

int moderatorQueue = -1;
char filteredWords[MAX_FILTERED_WORDS][MAX_WORD_LENGTH];
int wordCount = 0;
int violations[MAX_GROUPS][MAX_USERS] = {0};
int bannedUsers[MAX_GROUPS] = {0};
int messagesReceived = 0;
int groupsDeleted = 0;

void cleanup() {
	if (moderatorQueue != -1) {
    	msgctl(moderatorQueue, IPC_RMID, NULL);
	}
}

void signal_handler(int signum) {
	cleanup();
	exit(EXIT_FAILURE);
}

void loadFilteredWords(const char *filePath) {
	FILE *file = fopen(filePath, "r");
	if (!file) {
    	perror("Failed to open filtered words file");
    	exit(EXIT_FAILURE);
	}

	char line[MAX_WORD_LENGTH];
	while (fgets(line, sizeof(line), file) && wordCount < MAX_FILTERED_WORDS) {
    	size_t len = strlen(line);
    	if (len > 0 && line[len-1] == '\n') {
        	line[len-1] = '\0';
    	}
    	if (strlen(line) > 0) {
        	strncpy(filteredWords[wordCount], line, MAX_WORD_LENGTH - 1);
        	filteredWords[wordCount][MAX_WORD_LENGTH - 1] = '\0';
        	wordCount++;
    	}
	}
	fclose(file);
}

int countViolations(const char *message) {
	int uniqueViolations = 0;
	int foundWords[MAX_FILTERED_WORDS] = {0};
    
	char lowercaseMessage[MAX_TEXT_SIZE];
	strncpy(lowercaseMessage, message, MAX_TEXT_SIZE - 1);
	lowercaseMessage[MAX_TEXT_SIZE - 1] = '\0';
    
	for (char *p = lowercaseMessage; *p; p++) {
    	*p = tolower(*p);
	}

	for (int i = 0; i < wordCount; i++) {
    	if (foundWords[i]) continue;

    	char lowercaseFilter[MAX_WORD_LENGTH];
    	strncpy(lowercaseFilter, filteredWords[i], MAX_WORD_LENGTH - 1);
    	lowercaseFilter[MAX_WORD_LENGTH - 1] = '\0';
   	 
    	for (char *p = lowercaseFilter; *p; p++) {
        	*p = tolower(*p);
    	}

    	char *pos = lowercaseMessage;
    	while ((pos = strstr(pos, lowercaseFilter)) != NULL) {
        	int isWordStart = (pos == lowercaseMessage || !isalnum(*(pos - 1)));
        	int isWordEnd = (!*(pos + strlen(lowercaseFilter)) ||
                       	!isalnum(*(pos + strlen(lowercaseFilter))));
       	 
        	if (isWordStart && isWordEnd) {
            	foundWords[i] = 1;
            	uniqueViolations++;
            	break;
        	}
        	pos++;
    	}
	}

	return uniqueViolations;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
    	fprintf(stderr, "Usage: %s <test_case_number>\n", argv[0]);
    	exit(EXIT_FAILURE);
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	int testCaseNumber = atoi(argv[1]);
    
	char inputFilePath[256];
	snprintf(inputFilePath, sizeof(inputFilePath), "testcase_%d/input.txt", testCaseNumber);
    
	FILE *inputFile = fopen(inputFilePath, "r");
	if (!inputFile) {
    	perror("Failed to open input file");
    	exit(EXIT_FAILURE);
	}

	int numGroups, validationKey, appKey, moderatorKey, violationThreshold;
	if (fscanf(inputFile, "%d %d %d %d %d", &numGroups, &validationKey, &appKey,
           	&moderatorKey, &violationThreshold) != 5) {
    	fprintf(stderr, "Failed to read configuration\n");
    	fclose(inputFile);
    	exit(EXIT_FAILURE);
	}
	fclose(inputFile);

	char filteredWordsPath[256];
	snprintf(filteredWordsPath, sizeof(filteredWordsPath),
         	"testcase_%d/filtered_words.txt", testCaseNumber);
	loadFilteredWords(filteredWordsPath);

	moderatorQueue = msgget(moderatorKey, IPC_CREAT | 0666);
	if (moderatorQueue == -1) {
    	perror("Failed to create moderator message queue");
    	exit(EXIT_FAILURE);
	}

	Message msg;
	int activeGroups = numGroups;

	while (activeGroups > 0) {
    	if (msgrcv(moderatorQueue, &msg, sizeof(msg) - sizeof(msg.mtype), 1, 0) == -1) {
        	if (errno == EINTR) continue;
        	if (errno == EIDRM) {
            	printf("msgrcv failed: Identifier removed\n");
            	break;
        	}
        	perror("Message receive error");
        	break;
    	}

    	messagesReceived++;
   	 
    	int newViolations = countViolations(msg.mtext);
    	if (newViolations > 0) {
        	violations[msg.modifyingGroup][msg.user] += newViolations;
       	 
        	if (violations[msg.modifyingGroup][msg.user] >= violationThreshold &&
            	bannedUsers[msg.modifyingGroup] < MAX_USERS) {
           	 
            	Message banMsg = {
                	.mtype = 2,
                	.user = msg.user,
                	.modifyingGroup = msg.modifyingGroup
            	};
            	if (msgsnd(moderatorQueue, &banMsg, sizeof(Message) - sizeof(long), 0) == -1) {
                	if (errno == EIDRM) {
                    	printf("msgsnd failed: Identifier removed\n");
                    	break;
                	}
            	}
            	bannedUsers[msg.modifyingGroup]++;
        	}
    	}
	}

	cleanup();
	return 0;
}





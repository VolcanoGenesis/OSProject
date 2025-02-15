#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define MAX_FILTERED_WORDS 50
#define MAX_TEXT_SIZE 256
#define MAX_GROUPS 30
#define MAX_USERS 50

typedef struct {
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_TEXT_SIZE];
    int modifyingGroup;
} Message;

void loadFilteredWords(const char *filePath, char filteredWords[MAX_FILTERED_WORDS][MAX_TEXT_SIZE], int *wordCount) {
    FILE *file = fopen(filePath, "r");
    if (!file) {
        perror("Failed to open filtered words file");
        exit(EXIT_FAILURE);
    }

    *wordCount = 0;
    while (fgets(filteredWords[*wordCount], MAX_TEXT_SIZE, file) && *wordCount < MAX_FILTERED_WORDS) {
        // Remove newline character if present
        filteredWords[*wordCount][strcspn(filteredWords[*wordCount], "\n")] = 0;
        (*wordCount)++;
    }

    fclose(file);
}

int countViolations(const char *message, char filteredWords[MAX_FILTERED_WORDS][MAX_TEXT_SIZE], int wordCount) {
    int violations = 0;
    char lowercaseMessage[MAX_TEXT_SIZE];
    strcpy(lowercaseMessage, message);

    // Convert message to lowercase
    for (int i = 0; lowercaseMessage[i]; i++) {
        lowercaseMessage[i] = tolower(lowercaseMessage[i]);
    }

    // Check for each filtered word
    for (int i = 0; i < wordCount; i++) {
        char lowercaseFilteredWord[MAX_TEXT_SIZE];
        strcpy(lowercaseFilteredWord, filteredWords[i]);
        for (int j = 0; lowercaseFilteredWord[j]; j++) {
            lowercaseFilteredWord[j] = tolower(lowercaseFilteredWord[j]);
        }

        if (strstr(lowercaseMessage, lowercaseFilteredWord) != NULL) {
            violations++;
        }
    }

    return violations;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <test_case_number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int testCaseNumber = atoi(argv[1]);
    char inputFilePath[256];
    snprintf(inputFilePath, sizeof(inputFilePath), "testcase_%d/input.txt", testCaseNumber);

    FILE *inputFile = fopen(inputFilePath, "r");
    if (!inputFile) {
        perror("Failed to open input file");
        exit(EXIT_FAILURE);
    }

    int numGroups, validationKey, appKey, moderatorKey, violationThreshold;
    fscanf(inputFile, "%d %d %d %d %d", &numGroups, &validationKey, &appKey, &moderatorKey, &violationThreshold);
    fclose(inputFile);

    char filteredWordsPath[256];
    snprintf(filteredWordsPath, sizeof(filteredWordsPath), "testcase_%d/filtered_words.txt", testCaseNumber);

    char filteredWords[MAX_FILTERED_WORDS][MAX_TEXT_SIZE];
    int wordCount = 0;
    loadFilteredWords(filteredWordsPath, filteredWords, &wordCount);

    int moderatorQueue = msgget(moderatorKey, IPC_CREAT | 0666);
    if (moderatorQueue == -1) {
        perror("Failed to create moderator message queue");
        exit(EXIT_FAILURE);
    }

    int violations[MAX_GROUPS][MAX_USERS] = {0};
    Message msg;

    while (1) {
        if (msgrcv(moderatorQueue, &msg, sizeof(msg) - sizeof(msg.mtype), 1, 0) > 0) {
            int violationCount = countViolations(msg.mtext, filteredWords, wordCount);
            violations[msg.modifyingGroup][msg.user] += violationCount;

            if (violations[msg.modifyingGroup][msg.user] >= violationThreshold) {
                printf("User %d of Group %d banned after %d violations\n", 
                       msg.user, msg.modifyingGroup, violations[msg.modifyingGroup][msg.user]);

                // Send ban message to group
                Message banMsg;
                banMsg.mtype = 2; // Ban message type
                banMsg.user = msg.user;
                banMsg.modifyingGroup = msg.modifyingGroup;
                msgsnd(moderatorQueue, &banMsg, sizeof(banMsg) - sizeof(banMsg.mtype), 0);
            }
        }
    }

    return 0;
}


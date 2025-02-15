#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <libgen.h> // For dirname()
#include <errno.h>
#include <sys/stat.h> // For checking if files exist
#include <ctype.h>    // For trimming whitespaces
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

// Function to trim leading and trailing whitespaces
char *trimWhitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    return str;
}

void handleUserProcess(int pipeFd[2], const char *userFilePath) {
    close(pipeFd[0]); // Close read end of pipe
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
        write(pipeFd[1], buffer, MAX_TEXT_SIZE); // Write padded message to pipe
        usleep(10000); // Simulate delay
    }
    
    fclose(userFile);
    close(pipeFd[1]); // Close write end of pipe
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

    // Open group file
    FILE *groupFile = fopen(groupFilePath, "r");
    if (!groupFile) {
        perror("Failed to open group file");
        exit(EXIT_FAILURE);
    }

    // Read number of users and their file paths
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

        // Trim whitespaces from the user file path
        trimWhitespace(userFiles[i]);

        // Prepend test case directory path and users/ directory to user file paths
        char fullUserPath[MAX_TEXT_SIZE];
        snprintf(fullUserPath, sizeof(fullUserPath), "%s/../users/%s", dirname(strdup(groupFilePath)), userFiles[i]);
        strcpy(userFiles[i], fullUserPath);

        // Debugging: Print resolved path for verification
        printf("Resolved user file path: %s\n", fullUserPath);

        // Check if the resolved path exists
        struct stat buffer;
        if (stat(fullUserPath, &buffer) != 0) {
            fprintf(stderr, "Error: User file does not exist: %s (errno: %d)\n", fullUserPath, errno);
            perror("Stat error");
            fclose(groupFile);
            exit(EXIT_FAILURE);
        }
    }
    fclose(groupFile);

    // Create message queues
    int validationQueue = msgget(validationKey, IPC_CREAT | 0666);
    int appQueue = msgget(appKey, IPC_CREAT | 0666);
    int moderatorQueue = msgget(moderatorKey, IPC_CREAT | 0666);

    if (validationQueue == -1 || appQueue == -1 || moderatorQueue == -1) {
        perror("Failed to create message queues");
        exit(EXIT_FAILURE);
    }

    // Notify validation process about group creation
    Message msg;
    msg.mtype = 1; // Group creation message type
    msg.modifyingGroup = atoi(strrchr(groupFilePath, '_') + 1); // Extract group number from file name
    if (strrchr(groupFilePath, '_') == NULL) {
        fprintf(stderr, "Invalid group file path format: %s\n", groupFilePath);
        exit(EXIT_FAILURE);
    }
    msgsnd(validationQueue, &msg, sizeof(msg) - sizeof(msg.mtype), 0);

    // Create pipes and fork user processes
    int pipes[MAX_USERS][2];
    pid_t userPids[MAX_USERS];

    for (int i = 0; i < numUsers; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Failed to create pipe");
            exit(EXIT_FAILURE);
        }

        pid_t pid = fork();
        if (pid == 0) { // Child process (user)
            handleUserProcess(pipes[i], userFiles[i]);
            exit(0);
        } else if (pid > 0) { // Parent process (group)
            userPids[i] = pid;
            close(pipes[i][1]); // Close write end in parent
        } else {
            perror("Failed to fork user process");
            exit(EXIT_FAILURE);
        }
        
        // Notify validation process about new user creation
        msg.mtype = 2; // User creation message type
        msg.user = i;
        msgsnd(validationQueue, &msg, sizeof(msg) - sizeof(msg.mtype), 0);
    }

    // Process messages from users and send them to moderator/validation processes
    char buffer[MAX_TEXT_SIZE];
    int activeUsers = numUsers;
    fd_set readfds;
    
    while (activeUsers > 1) {
        FD_ZERO(&readfds);
        int maxfd = -1;
        for (int i = 0; i < numUsers; i++) {
            FD_SET(pipes[i][0], &readfds);
            if (pipes[i][0] > maxfd) maxfd = pipes[i][0];
        }

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) > 0) {
            for (int i = 0; i < numUsers; i++) {
                if (FD_ISSET(pipes[i][0], &readfds)) {
                    ssize_t bytesRead = read(pipes[i][0], buffer, MAX_TEXT_SIZE);
                    if (bytesRead > 0) {
                        // Process and forward message
                        Message msg;
                        sscanf(buffer, "%d %[^\n]", &msg.timestamp, msg.mtext);
                        msg.mtype = 1; // Message type for moderator
                        msg.user = i;
                        msg.modifyingGroup = atoi(strrchr(groupFilePath, '_') + 1);

                        // Send to moderator
                        msgsnd(moderatorQueue, &msg, sizeof(msg) - sizeof(msg.mtype), 0);

                        // Send to validation
                        msgsnd(validationQueue, &msg, sizeof(msg) - sizeof(msg.mtype), 0);
                    } else if (bytesRead == 0) {
                        // User process has finished
                        close(pipes[i][0]);
                        activeUsers--;
                    }
                }
            }
        }

        // Check for ban messages from moderator
        Message banMsg;
        if (msgrcv(moderatorQueue, &banMsg, sizeof(banMsg) - sizeof(banMsg.mtype), 2, IPC_NOWAIT) > 0) {
            // Handle user ban
            close(pipes[banMsg.user][0]);
            activeUsers--;
        }
    }

    // Notify app about group termination
    Message terminationMsg;
    terminationMsg.mtype = 1;
    terminationMsg.modifyingGroup = atoi(strrchr(groupFilePath, '_') + 1);
    msgsnd(appQueue, &terminationMsg, sizeof(terminationMsg) - sizeof(terminationMsg.mtype), 0);

    // Cleanup
    for (int i = 0; i < numUsers; i++) {
        close(pipes[i][0]);
    }

    return 0;
}





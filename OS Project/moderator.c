#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#define MAX_WORDS 50
#define MAX_MESSAGE_LENGTH 256

typedef struct {
    long mtype;
    int user;
    char mtext[MAX_MESSAGE_LENGTH];
    int violations;
    int grp_id;
    int action;
    int terminator;
} ModeratorMessage;

char filtered_words[MAX_WORDS][25];
int num_filtered_words = 0;

void load_filtered_words(char *test_case) {
    char filename[256];
    snprintf(filename, sizeof(filename), "testcase_%s/filtered_words.txt", test_case);
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening filtered_words.txt");
        exit(EXIT_FAILURE);
    }
    while (fscanf(file, "%20s", filtered_words[num_filtered_words]) != EOF && num_filtered_words < MAX_WORDS) {
        num_filtered_words++;
    }
    fclose(file);
}

int count_violations(char *message) {
    int violations = 0;
    for (int i = 0; i < num_filtered_words; i++) {
        if (strcasestr(message, filtered_words[i]) != NULL) {
            violations++;
        }
    }
    return violations;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <test_case_number>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    load_filtered_words(argv[1]);
    
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
    fclose(file);
    
    printf("Moderator: Using message queue key: %d\n", mod_msgq_key);
    int msgq_id = msgget(mod_msgq_key, IPC_CREAT | 0666);
    if (msgq_id == -1) {
        perror("msgget failed");
        return EXIT_FAILURE;
    }
    
    printf("Moderator process started. Waiting for messages...\n");
    ModeratorMessage msg;
    while (1) {
        if (msgrcv(msgq_id, &msg, sizeof(ModeratorMessage) - sizeof(long), 1, 0) < 0) {
            perror("msgrcv failed");
            sleep(1);
            continue;
        }
        printf("Message received: %s, user: %d, violations: %d, group_id: %d\n",msg.mtext, msg.user, msg.violations, msg.grp_id);
        int violations = count_violations(msg.mtext);
        ModeratorMessage reply;
        reply.mtype = 50 + msg.grp_id;
        reply.violations = msg.violations;
        reply.action = (reply.violations >=violation_threshold) ? 1 : 0;
        msgsnd(msgq_id,&reply, sizeof(ModeratorMessage) - sizeof(long), 0);
        if (reply.action > 0) {
            printf("User %d from group %d has been removed due to %d violations.\n",msg.user, msg.grp_id, reply.violations);
        }
        
    }
    return EXIT_SUCCESS;
}

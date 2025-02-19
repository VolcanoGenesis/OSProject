#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_USERS 50
#define MAX_MESSAGE_LENGTH 256
#define MAX_GROUPS 30
#define FILTER_WORD_MAX 22

// Structure to send messages to validation and moderator.
typedef struct {
    long mtype;
    int timestamp;
    int user;
    char mtext[MAX_MESSAGE_LENGTH];
    int modifyingGroup;
} Message;

// Structure to track each user's message stream.
typedef struct {
    int timestamp;
    char mtext[MAX_MESSAGE_LENGTH];
    int valid;                // 1 if a valid message is stored.
    int active;               // 1 if user is not banned.
    int finished;             // 1 if user finished sending messages normally.
    int cumulative_violations; // Cumulative violation count.
    FILE *fp;                 // Stream for reading from the user pipe.
    int user_id;
} UserMsgRecord;


typedef struct {
    long mtype;
    int user;
    char mtext[MAX_MESSAGE_LENGTH];
    int violations;
    int grp_id;
    int action;
    int terminator;
} ModeratorMessage;
// Global filtered words array.

char filtered_words[MAX_USERS][FILTER_WORD_MAX];
int num_filtered_words = 0;

void load_filtered_words(const char *test_case) {
    char filename[256];
    snprintf(filename, sizeof(filename), "testcase_%s/filtered_words.txt", test_case);
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening filtered_words.txt in group.out");
        exit(EXIT_FAILURE);
    }
    while (num_filtered_words < MAX_USERS && fscanf(file, "%20s", filtered_words[num_filtered_words]) == 1) {
        num_filtered_words++;
    }
    fclose(file);
}

int count_violations(const char *message) {
    int violations = 0;
    for (int i = 0; i < num_filtered_words; i++) {
        if (strcasestr(message, filtered_words[i]) != NULL)
            violations++;
    }
    return violations;
}

// Helper to extract group number from filename (assumes "group_X.txt")
int get_group_number(const char *group_file_path) {
    const char *p = strstr(group_file_path, "group_");
    if (!p) return -1;
    p += 6;
    return atoi(p);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <test_case_number> <group_file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    // Load filtered words.
    load_filtered_words(argv[1]);
    
    // Read keys and threshold from input.txt in the testcase folder.
    char input_file[256];
    snprintf(input_file, sizeof(input_file), "testcase_%s/input.txt", argv[1]);
    FILE *inputF = fopen(input_file, "r");
    if (!inputF) {
        perror("Error opening input.txt in group.out");
        return EXIT_FAILURE;
    }
    int num_groups; 
    int val_msgq_key; 
    int app_msgq_key; 
    int mod_msgq_key; 
    int violation_threshold;
    if (fscanf(inputF, "%d %d %d %d %d", &num_groups, &val_msgq_key, &app_msgq_key, &mod_msgq_key, &violation_threshold) != 5) {
        fprintf(stderr, "Error reading input.txt\n");
        fclose(inputF);
        return EXIT_FAILURE;
    }
    fclose(inputF);
    
    // Extract group number from group file name.
    char group_file_path[256];
    strncpy(group_file_path, argv[2], sizeof(group_file_path));
    group_file_path[sizeof(group_file_path)-1] = '\0';
    int group_number = get_group_number(group_file_path);
    if (group_number < 0) {
        fprintf(stderr, "Could not extract group number from %s\n", group_file_path);
        return EXIT_FAILURE;
    }
    
    // Open group file to get user file paths.
    FILE *groupF = fopen(group_file_path, "r");
    if (!groupF) {
        perror("Error opening group file");
        return EXIT_FAILURE;
    }
    int num_users;
    if (fscanf(groupF, "%d", &num_users) != 1) {
        fprintf(stderr, "Error reading number of users in group file\n");
        fclose(groupF);
        return EXIT_FAILURE;
    }
    char user_files[MAX_USERS][256];
    for (int i = 0; i < num_users; i++) {
        if (fscanf(groupF, "%s", user_files[i]) != 1) {
            fprintf(stderr, "Error reading user file path\n");
            fclose(groupF);
            return EXIT_FAILURE;
        }
    }
    fclose(groupF);
    
    // Send "group created" message to validation.out (mtype = 1).
    int val_msq_id = msgget(val_msgq_key, IPC_CREAT | 0666);
    if (val_msq_id == -1) {
        perror("msgget failed for validation queue in group.out");
        return EXIT_FAILURE;
    }
    int mod_msq_id = msgget(mod_msgq_key, IPC_CREAT | 0666);
      if (mod_msq_id == -1) {
           perror("mod key cannot be retrieved");
           return EXIT_FAILURE;
        }
    Message msg;
    msg.mtype = 1;
    msg.modifyingGroup = group_number;
    msgsnd(val_msq_id, &msg, sizeof(Message) - sizeof(long), 0);
    
    // Create a pipe for each user and fork a child process to read from the corresponding user file.
    int pipes[MAX_USERS][2];
    pid_t pids[MAX_USERS];
    for (int i = 0; i < num_users; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe failed");
            return EXIT_FAILURE;
        }
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork failed");
            return EXIT_FAILURE;
        }
        if (pids[i] == 0) { // Child process
            close(pipes[i][0]);
            char full_user_path[512];
            snprintf(full_user_path, sizeof(full_user_path), "testcase_%s/%s", argv[1], user_files[i]);
            FILE *userF = fopen(full_user_path, "r");
            if (!userF) {
                perror("Error opening user file in child");
                exit(EXIT_FAILURE);
            }
            char line[300];
            while (fgets(line, sizeof(line), userF) != NULL) {
                write(pipes[i][1], line, strlen(line));
            }
            fclose(userF);
            close(pipes[i][1]);
            exit(EXIT_SUCCESS);
        }
        close(pipes[i][1]); // Parent closes write end.
    }
    
    // Initialize records for each user.
    FILE *user_streams[MAX_USERS];
    UserMsgRecord records[MAX_USERS];
    for (int i = 0; i < num_users; i++) {
        user_streams[i] = fdopen(pipes[i][0], "r");
        if (!user_streams[i]) {
            perror("fdopen failed");
            return EXIT_FAILURE;
        }
        char buffer[300];
        if (fgets(buffer, sizeof(buffer), user_streams[i]) != NULL) {
            int ts;
            char msg_text[MAX_MESSAGE_LENGTH];
            if (sscanf(buffer, "%d %s", &ts, msg_text) == 2) {
                records[i].timestamp = ts;
                strncpy(records[i].mtext, msg_text, MAX_MESSAGE_LENGTH);
                records[i].valid = 1;
            } else {
                records[i].valid = 0;
            }
        } else {
            records[i].valid = 0;
        }
        records[i].active = 1;   // Initially, user is active.
        records[i].finished = 0; // Not finished.
        records[i].cumulative_violations = 0;
        records[i].user_id = atoi(strrchr(user_files[i], '_')+1);
       // printf("User created %d %s", records[i].user_id, user_files[i]);
        records[i].fp = user_streams[i];
        
        Message userAdded;
        userAdded.mtype = 2;
        userAdded.user = records[i].user_id;
        userAdded.modifyingGroup = group_number;
        
        if (msgsnd(val_msq_id, &userAdded, sizeof(Message)-sizeof(long), 0) == -1){
        perror("msgsnd failed to send mesaages");
        }
    }
    int users = num_users;
    int banned_count = 0;
    // Merging loop: process messages in ascending order.
    while (users > 1) {
        int min_index = -1;
        
        for (int i = 0; i < num_users; i++) {
            if (records[i].valid && records[i].active) {
                if (min_index == -1 || records[i].timestamp < records[min_index].timestamp)
                    min_index = i;
            }
        }
        if (min_index == -1)
            break;  // No more valid messages from active users.
        
        int v = count_violations(records[min_index].mtext);
        records[min_index].cumulative_violations += v;
        
        
        if (users < 2) break;
       
        msg.mtype = MAX_GROUPS + group_number;
        msg.timestamp = records[min_index].timestamp;
        msg.user = records[min_index].user_id;
        strncpy(msg.mtext, records[min_index].mtext, MAX_MESSAGE_LENGTH);
        msg.modifyingGroup = group_number;
        printf("Sending message %s, time : %d, users : %d Violations : %d\n", msg.mtext, msg.timestamp, users, records[min_index].cumulative_violations);
        if (msgsnd(val_msq_id, &msg, sizeof(Message) - sizeof(long), 0) == -1){
        printf("Failed with message %s, user %d, timestamp %d, grp No : %d\n", msg.mtext, msg.user, msg.timestamp, group_number);
        return 0;
        }
         // Prepare and send a message to moderator.
        ModeratorMessage modMsg;
        modMsg.mtype = 1;  // Group sends with mtype 1.
        modMsg.user = records[min_index].user_id;
        strncpy(modMsg.mtext, records[min_index].mtext, MAX_MESSAGE_LENGTH);
        modMsg.violations = records[min_index].cumulative_violations;
        modMsg.grp_id = group_number;
        modMsg.action = 0;
        if (msgsnd(mod_msq_id, &modMsg, sizeof(ModeratorMessage) - sizeof(long), 0) ==-1) {
             perror("msgsnd failed");
        }
        // Wait for moderator reply with mtype = 50 + group_number.
        ModeratorMessage reply;
        if (msgrcv(mod_msq_id, &reply, sizeof(ModeratorMessage) - sizeof(long), 50 + group_number, 0) < 0) {
             perror("msgrcv failed");
        }
        
        if (reply.action == 1) {
             // Moderator instructs to ban this user.
             records[min_index].active = 0;
             banned_count++;
             users--;
             printf("User %d from group %d banned by moderator (violations: %d).\n",
                    records[min_index].user_id, group_number, reply.violations);
             // Optionally, update record from this user.
             continue;  // Skip further processing for this message.
        }
        char buffer[300];
        if (fgets(buffer, sizeof(buffer), records[min_index].fp) != NULL) {
            int ts;
            char msg_text[MAX_MESSAGE_LENGTH];
            if (sscanf(buffer, "%d %s", &ts, msg_text) == 2) {
                records[min_index].timestamp = ts;
                strncpy(records[min_index].mtext, msg_text, MAX_MESSAGE_LENGTH);
                int slen = strlen(records[min_index].mtext);
                records[min_index].mtext[slen] = '\0';
                records[min_index].valid = 1;
          } 
          else{
                records[min_index].valid = 0;
            }
        } else {
            users--;
            records[min_index].finished = 1;
            records[min_index].valid = 0;
        }
    }
    
    for (int i = 0; i < num_users; i++) {
        wait(NULL);
    }
    
    // Count banned users.
   
 
    // Termination message: send the number of users removed due to violations.
    Message term_msg;
    term_msg.mtype = 3;
    term_msg.modifyingGroup = group_number;
    term_msg.user = banned_count;  // Send number of banned users.
    msgsnd(val_msq_id, &term_msg, sizeof(Message) - sizeof(long), 0);

    printf("All users terminated. Exiting group process %d. (Banned: %d)\n",
           group_number, banned_count);
    return EXIT_SUCCESS;
}

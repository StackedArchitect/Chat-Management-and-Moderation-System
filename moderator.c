#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ctype.h>
#include <stdbool.h>

#define FWLENGTH 50 // filtered words length made a blunder here 
#define MAX_WORD 50
#define MAX_MSG 256
#define MAX_USERS 50
#define MAX_GROUPS 30
#define MAX_PATH 256


typedef struct {
    long mtype;
    int group_id;
    int user_id;
    char message[MAX_MSG];
} ModMessage;

typedef struct {
    long mtype;
    int user_id;
    int violations;
} ModResponse;

struct UserViolations {
    int violations;
    int active;
};

void to_lower(char *str) {//making lowercase
    for (int i = 0; str[i]; i++) {
        str[i] = tolower(str[i]);
    }
}

int contains_word(const char *message, const char *word) {//substring check
    char msg_lower[MAX_MSG];
    char word_lower[MAX_WORD];
    
    strncpy(msg_lower, message, MAX_MSG - 1);
    msg_lower[MAX_MSG - 1] = '\0';
    strncpy(word_lower, word, MAX_WORD - 1);
    word_lower[MAX_WORD - 1] = '\0';
    
    to_lower(msg_lower);
    to_lower(word_lower);
    
    return strstr(msg_lower, word_lower) != NULL;
}
void grp_extract(int test_case, int *group_list, int *num_groups) {
    char fpath[MAX_PATH];
    snprintf(fpath, sizeof(fpath), "testcase_%d/input.txt", test_case);

    FILE *fp = fopen(fpath, "r");
    if (!fp) {
        printf("cant open input file\n");
        exit(1);
    }

    int tot_grps, val_key, app_key, mod_key, thres;
    fscanf(fp, "%d %d %d %d %d", &tot_grps, &val_key, &app_key, &mod_key, &thres);

    *num_groups = tot_grps;

    for (int i = 0; i < tot_grps; i++) {
        char group_path[MAX_PATH];
        if (fscanf(fp, "%s", group_path) != 1) {
            printf("Error reading group file path for group %d\n", i);
            fclose(fp);
            exit(1);
        }
        sscanf(group_path, "groups/group_%d.txt", &group_list[i]);
    }

    fclose(fp);
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <test_case>\n", argv[0]);
        exit(1);
    }

    int test_case = atoi(argv[1]);
    
    // Read filtered words
    char fil_words[FWLENGTH][MAX_WORD];
    int num_fil_words = 0;
    
    char fpath[256];
    snprintf(fpath, sizeof(fpath), "testcase_%d/filtered_words.txt", test_case);
    
    FILE *fp = fopen(fpath, "r");
    if (!fp) {
        printf("Error opening filtered words file\n");
        exit(1);
    }

    while (num_fil_words < FWLENGTH && 
           fgets(fil_words[num_fil_words], MAX_WORD, fp)) {


        // Remove newline if present
        char *newline = strchr(fil_words[num_fil_words], '\n');
        if (newline) *newline = '\0';
        num_fil_words++;
    }
    fclose(fp);

// Read input file
    snprintf(fpath, sizeof(fpath), "testcase_%d/input.txt", test_case);
    fp = fopen(fpath, "r");
    if (!fp) {
        printf("Error opening input file\n");
        exit(1);
    }

    int num_groups, val_key, app_key, mod_key, thres;
    fscanf(fp, "%d %d %d %d %d", &num_groups, &val_key, &app_key, &mod_key, &thres);
    fclose(fp);

    // Connect to message queue
    int msgq_id = msgget(mod_key, IPC_CREAT | 0666);//cereating here it was not found before


    printf("Moderator connected to message queue %d\n", msgq_id);
    if (msgq_id == -1) {
        printf("Error connecting to message queue\n");
        exit(1);
    }

    struct UserViolations violations[MAX_GROUPS][MAX_USERS] = {0};//violations track

    printf("Moderator initialized with %d filtered words\n", num_fil_words);

    for (int i = 0; i < num_fil_words; i++) {
        printf("Filtered word %d: '%s'\n", i, fil_words[i]);
    }
    
    // Process messages
    ModMessage msg = {0};  // Initialize message structure
    while (1) {
        int group_list[MAX_GROUPS];
        int num_groups;
        grp_extract(test_case, group_list, &num_groups);

        
        for (int i = 0; i < num_groups; i++) {
            int group = group_list[i];
            
            // Clear message buffer before receiving
            memset(&msg, 0, sizeof(ModMessage));
            
            if(msgrcv(msgq_id, &msg, sizeof(ModMessage) - sizeof(long), group+100, IPC_NOWAIT)==-1){
                        //printf("No message received from group %d\n", group);
                continue;
            }
            

            // Verify message belongs to correct group and has valid data
            if (msg.group_id != group || msg.group_id < 0 || msg.user_id < 0) {
                printf("Moderator received message from group %d but group_id=%d\n", group,msg.group_id);
                printf("[Moderator] Invalid message: group=%d, user=%d\n", 
                       msg.group_id, msg.user_id);
                continue;
            }

            // Ensure message is null-terminated
            msg.message[MAX_MSG - 1] = '\0';

            // Count violations in this message
            int new_violations = 0;
            for (int j = 0; j < num_fil_words; j++) {
                if (contains_word(msg.message, fil_words[j])) {
                    printf("[Moderator] Found violation '%s' in message '%s' from user %d in group %d\n",
                           fil_words[j], msg.message, msg.user_id, msg.group_id);
                    new_violations++;
                }
            }

            violations[msg.group_id][msg.user_id].violations += new_violations;
            
            // Send response with updated violation count
            ModResponse response = {0}; 
            response.mtype = group+300;       //we need to do this to maintain order
            response.user_id = msg.user_id;
            response.violations = violations[group][msg.user_id].violations;

            usleep(1000);

            if (msgsnd(msgq_id, &response, sizeof(ModResponse) - sizeof(long), 0) != -1) {
                if (new_violations > 0) {
                    printf("[Moderator] User %d in group %d now has %d violations (thres: %d)\n",
                           msg.user_id, group, response.violations, thres);
                }
            } else {
                printf("[Moderator] Error sending response to group %d\n", group);
            }
        }
        usleep(1000);
    }

    return 0;
}
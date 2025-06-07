#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_GROUPS 30
#define MAX_PATH 256
#define MAXMSG_LEN 256
#define PMSIZE 256
#define GRPTERM 2 

typedef struct
{
    long mtype;
    int timestamp;
    int user;
    char mtext[MAXMSG_LEN];
    int modifyingGroup;
} ValMessage;

typedef struct
{
    long mtype;
    int group_id;
    int user_id;
    char message[MAXMSG_LEN];
} ModMessage;

typedef struct {
    long mtype;     
    int user_id;    
    int voils; 
} ModResponse;

typedef struct
{
    long mtype;
    int group_id;
    int status;
} AppMessage;

struct Message
{
    int timestamp;
    int user_id;
    char message[MAXMSG_LEN];
};

struct UserState
{
    pid_t pid;
    int pipe_fd;
    int file_id;   //actual userid i have taken array iindex as suer id but now using id from filename
    bool is_banned;
    bool is_active;
    bool removed;
};

static void merge(struct Message arr[], int l, int mid, int r) {
    int l_size = mid - l + 1;
    int r_size = r - mid;

    struct Message *l_arr = malloc(l_size * sizeof(struct Message));
    struct Message *r_arr = malloc(r_size * sizeof(struct Message));

    for (int i = 0; i < l_size; i++) {
        l_arr[i] = arr[l + i];
    }
    for (int i = 0; i < r_size; i++) {
        r_arr[i] = arr[mid + 1 + i];
    }

    int i = 0, j = 0, k = l;
    while (i < l_size && j < r_size) {
        if (l_arr[i].timestamp < r_arr[j].timestamp || 
            (l_arr[i].timestamp == r_arr[j].timestamp && l_arr[i].user_id < r_arr[j].user_id)) {
            arr[k] = l_arr[i];
            i++;
        } else {
            arr[k] = r_arr[j];
            j++;
        }
        k++;
    }

    while (i < l_size) {
        arr[k] = l_arr[i];
        i++;
        k++;
    }
    while (j < r_size) {
        arr[k] = r_arr[j];
        j++;
        k++;
    }

    free(l_arr);
    free(r_arr);
}

static void merge_sort(struct Message arr[], int l, int r) {
    if (l < r) {
        int mid = l + (r - l) / 2;
        merge_sort(arr, l, mid);
        merge_sort(arr, mid + 1, r);
        merge(arr, l, mid, r);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 8)
    {
        printf("wrong input arguments\n");
        exit(1);
    }

    int test_case = atoi(argv[1]);
    int grp_num = atoi(argv[2]);
    key_t val_key = atoi(argv[3]);
    key_t app_key = atoi(argv[4]);
    key_t mod_key = atoi(argv[5]);
    int thres = atoi(argv[6]);
    char *gfile = argv[7];

    int val_qid = msgget(val_key, 0666);
    int app_qid = msgget(app_key, 0666);
    int mod_qid = msgget(mod_key, 0666);

    if (val_qid == -1 || app_qid == -1 || mod_qid == -1)
    {
        printf("msgget failed\n");
        exit(1);
    }

    FILE *gf = fopen(gfile, "r");
    if (!gf)
    {
        printf("fopen failed\n");
        exit(1);
    }

    int num_users;
    if (fscanf(gf, "%d", &num_users) != 1)
    {
        printf("Error reading number of users\n");
        fclose(gf);
        exit(1);
    }

    char user_files[num_users][MAX_PATH];
    for (int i = 0; i < num_users; i++)
    {
        if (fscanf(gf, "%255s", user_files[i]) != 1)
        {
            printf("Error reading user file name\n");
            fclose(gf);
            exit(1);
        }
    }
    fclose(gf);

    ValMessage gc_msg = {//group creation msg
        .mtype = 1,
        .modifyingGroup = grp_num};
    if (msgsnd(val_qid, &gc_msg, sizeof(ValMessage) - sizeof(long), 0) == -1)
    {
        printf("msgsnd of group creation fail\n");
        exit(1);
    }

    struct UserState *users = calloc(num_users, sizeof(struct UserState));
    int pipes[num_users][2];

    for (int i = 0; i < num_users; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            printf("pipe failed\n");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == -1)
        {
            printf("fork failed\n");
            exit(1);
        }

        if (pid == 0)
        { // Child (user process)
            close(pipes[i][0]);

            char upath[MAX_PATH];//usr file path
            snprintf(upath, MAX_PATH, "testcase_%d/%s", test_case, user_files[i]);

            FILE *uf = fopen(upath, "r");
            if (!uf)
            {
                printf("fopen failed for userfile\n");
                close(pipes[i][1]);
                exit(1);
            }

            char line[PMSIZE];
            while (fgets(line, sizeof(line), uf))
            {
                char buf[PMSIZE] = {0};
                strncpy(buf, line, PMSIZE - 1);
                write(pipes[i][1], buf, PMSIZE);
            }

            fclose(uf);
            close(pipes[i][1]);
            exit(0);
        }

        close(pipes[i][1]);
        users[i].pid = pid;
        users[i].pipe_fd = pipes[i][0];
        users[i].is_active = true;
        users[i].is_banned = false;

        // Extract user ID from filename and store in UserState which is actual user state
        char *un = strrchr(user_files[i], '_');
        char *dot = strchr(un + 1, '.');
        char id_str[10] = {0};
        strncpy(id_str, un + 1, dot - un - 1);
        users[i].file_id = atoi(id_str);

        ValMessage val_msg = {
            .mtype = 2,
            .user = users[i].file_id,
            .modifyingGroup = grp_num};
        if (msgsnd(val_qid, &val_msg, sizeof(ValMessage) - sizeof(long), 0) == -1)
        {
            printf("msgsnd of user creation fail\n");
            exit(1);
        }
    }

    struct Message *msgs = NULL;
    int num_msgs = 0;
    int active_users = num_users;
    int rem_users = num_users;
    int busers = 0; // Initialize banned users count

    while (active_users > 0)
    {
        for (int i = 0; i < num_users; i++)
        {
            if (!users[i].is_active)
                continue;

            char b[PMSIZE];
            ssize_t n = read(users[i].pipe_fd, b, PMSIZE);

            if (n > 0)
            {
                int timestamp;
                char message[MAXMSG_LEN];
                if (sscanf(b, "%d %255s", &timestamp, message) == 2)
                {
                    msgs = realloc(msgs, (num_msgs + 1) * sizeof(struct Message));
                    msgs[num_msgs].timestamp = timestamp;
                    msgs[num_msgs].user_id = i; // Use array index
                    strncpy(msgs[num_msgs].message, message, MAXMSG_LEN - 1);
                    msgs[num_msgs].message[MAXMSG_LEN - 1] = '\0';
                    num_msgs++;
                }
            }
            else if (n == 0)
            {
                users[i].is_active = false;
                close(users[i].pipe_fd);
                active_users--;
            }
            else if (n == -1)
            {
                printf("cant read pipe\n");
                users[i].is_active = false; // Mark user as inactive on error
                active_users--;
                close(users[i].pipe_fd); 
                continue;                // Skip to the next user
            }
        }
    }

    merge_sort(msgs, 0, num_msgs - 1);
    printf("[Group %d] Sorted %d msgs by time\n", grp_num, num_msgs);

                // Use integer states instead of booleans for tracking valid users
                              // -1: not in group, 0: in group, 1: banned, 2: removed
    int *ustate = calloc(50, sizeof(int));  //user states
    for (int i = 0; i < 50; i++) {
        ustate[i] = -1;  // Mark as not in group
    }
    
// Mark users in group
    for (int i = 0; i < num_users; i++) {
        ustate[users[i].file_id] = 0;  // Mark as in group
    }

    int *umsg_count = calloc(num_users, sizeof(int));
    for (int i = 0; i < num_msgs; i++) {
        int idx = msgs[i].user_id;
        umsg_count[idx]++;
    }

    bool group_terminated = false;

    // Process msgs in timestamp order
    for (int i = 0; i < num_msgs && !group_terminated; i++) {
        int array_idx = msgs[i].user_id;
        int actual_user_id = users[array_idx].file_id;

        // Skip if user is not in group, banned, or removed
        if (ustate[actual_user_id] != 0) {
            umsg_count[array_idx]--;
            continue;
        }

// Send message to moderator
        ModMessage mod_msg = {0};
        mod_msg.mtype = grp_num + 100;
        mod_msg.group_id = grp_num;
        mod_msg.user_id = actual_user_id;
        strncpy(mod_msg.message, msgs[i].message, MAXMSG_LEN - 1);
        mod_msg.message[MAXMSG_LEN - 1] = '\0';

//send msg to mod
        if (msgsnd(mod_qid, &mod_msg, sizeof(ModMessage) - sizeof(long), 0) == -1) {
            printf("msgsnd failed to mod\n");
            continue;
        }

//recevie msg from mod
        ModResponse mod_resp = {0};
        if (msgrcv(mod_qid, &mod_resp, sizeof(ModResponse) - sizeof(long),grp_num + 300, 0) == -1) {//made mtype grou no+300 so that it doesnt collide with sending ones
            printf("msgrcv failed from mod\n");
            continue;
        }

        // First send message to validation (if user is still valid)
        if (ustate[actual_user_id] == 0) { 
             // User is still in group
            ValMessage val_msg = {0};

            val_msg.mtype = MAX_GROUPS + grp_num;

            val_msg.timestamp = msgs[i].timestamp;
            val_msg.user = actual_user_id;
            val_msg.modifyingGroup = grp_num;

            strncpy(val_msg.mtext, msgs[i].message, MAXMSG_LEN - 1);
            val_msg.mtext[MAXMSG_LEN - 1] = '\0';

            usleep(1000);

            if (msgsnd(val_qid, &val_msg, sizeof(ValMessage) - sizeof(long), 0) == -1) {
                printf("msgsnd failed to val\n");
            }
        }

        // Then check for voils and handle user banning
        if (mod_resp.voils >= thres) {
            if (ustate[actual_user_id] == 0) {  // Only if user is still active
                ustate[actual_user_id] = 1;  // Mark as banned
                rem_users--;
                busers++;

                printf("User %d from group %d has been removed due to %d voilations.\n",
                       actual_user_id, grp_num, mod_resp.voils);

                if (rem_users < 2) {
                    group_terminated = true;
                }
            }
        }

        // Update message count and check if user is done
        umsg_count[array_idx]--;
        if (umsg_count[array_idx] == 0 && ustate[actual_user_id] == 0) {

            ustate[actual_user_id] = 2;  // Mark as removed (finished msgs)
            rem_users--;

            if (rem_users < 2) {
                group_terminated = true;
            }
        }
    }

    // Handle group termination
    if (group_terminated || rem_users < 2) {//check if group is terminated

        ValMessage term_msg = {
            .mtype = 3,
            .modifyingGroup = grp_num,
            .user = busers
        };

        if (msgsnd(val_qid, &term_msg, sizeof(ValMessage) - sizeof(long), 0) == -1) {
            perror("msgsnd termination to validation failed");
        }

        AppMessage app_msg = {//send also to grop regarding termination
            .mtype = GRPTERM,
            .group_id = grp_num,
            .status = 0
        };
        if (msgsnd(app_qid, &app_msg, sizeof(AppMessage) - sizeof(long), 0) == -1) {
            perror("msgsnd termination to app failed");
        }
    }

    free(msgs);
    free(users);
    free(umsg_count);
    free(ustate);
    return 0;
}
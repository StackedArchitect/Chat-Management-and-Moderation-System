
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>

#define MAX_PATH 256
#define MAX_GROUPS 30


struct msg_buffer
{
    long mtype;
    int grp_id;
    int status; 
};

#define GRPTERM 2

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Error: Wrong usage\n");
        printf("Give the test case number as argument\n");
        return 1;
    }

    int ng;
    int val_key;
    int ag_key;
    int mg_key;
    int thres;
    char group_files[MAX_GROUPS][MAX_PATH];

    char input_path[MAX_PATH];
    sprintf(input_path, "testcase_%s/input.txt", argv[1]);

    FILE *input_file = fopen(input_path, "r");
    if (input_file == NULL) {
        printf("Error: Cannot open input file\n");
        return 1;
    }

    if (fscanf(input_file, "%d %d %d %d %d",
        &ng,
        &val_key,
        &ag_key,
        &mg_key,
        &thres) != 5) {
        printf("Error: Cannot read input parameters\n");
        fclose(input_file);
        return 1;
    }




    char line[MAX_PATH];
    fgets(line, MAX_PATH, input_file);
    for (int i = 0; i < ng; i++) {

        if (fgets(line, MAX_PATH, input_file) == NULL) {

            printf("Error: Cannot read group %d file path\n", i);
            fclose(input_file);
            return 1;
        }


        line[strcspn(line, "\n")] = 0; 
        sprintf(group_files[i], "testcase_%s/%s", argv[1], line);
        printf("Group %d file: %s\n", i, group_files[i]);
    }
    fclose(input_file);

     // Connect to message queues
    int val_qid;
     while ((val_qid = msgget(val_key, 0666)) == -1) { 
        printf("Error connecting to validation message queue in app.c\n");
        usleep(100000);
    }

    int ag_qid = msgget(ag_key, IPC_CREAT | 0666); // Create app-group message queue
    if (ag_qid == -1) {
        perror("Error creating app-group message queue");
        return 1;
    }

    int mg_qid;
    while ((mg_qid = msgget(mg_key, 0666)) == -1) {
        printf("Error connecting to moderator message queue in app.c\n");
        usleep(100000);
    }


    int active_grps = ng;
    pid_t gpids[MAX_GROUPS];

    for (int i = 0; i < ng; i++)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            printf("Error: Fork failed\n");
            return 1;
        }

        if (pid == 0) { // Child process (group process)

            char *group_number_str = strrchr(group_files[i], '_') + 1;
            char group_number_copy[MAX_PATH];
            strncpy(group_number_copy, group_number_str, MAX_PATH - 1);
            group_number_copy[MAX_PATH - 1] = '\0';

            char *dot_txt = strchr(group_number_copy, '.');
            if(dot_txt != NULL){
                *dot_txt = '\0';
            }
            int group_number = atoi(group_number_copy);

            char group_num_str[10], v_key[20], app_key[20];
            char mod_key[20], viol_thresh[10];

            snprintf(group_num_str, sizeof(group_num_str), "%d", group_number);
            snprintf(v_key, sizeof(v_key), "%d", val_key);
            snprintf(app_key, sizeof(app_key), "%d", ag_key);
            snprintf(mod_key, sizeof(mod_key), "%d", mg_key);
            snprintf(viol_thresh, sizeof(viol_thresh), "%d", thres);

            execl("./groups.out", "groups.out",
                  argv[1],
                  group_num_str,
                  v_key,
                  app_key,
                  mod_key,
                  viol_thresh,
                  group_files[i],
                  NULL);

            printf("Error: Cannot execute groups program\n");
            return 1;
        }

        gpids[i] = pid;
    }

    // Wait for group terminations
    struct msg_buffer msg;
    while (active_grps > 0) {
       if (msgrcv(ag_qid, &msg, sizeof(msg) - sizeof(long), GRPTERM, 0) > 0) { 
            if (msg.status == 0) {
                printf("All users terminated. Exiting group process %d.\n", msg.grp_id);
                active_grps--;
            }
        }
    }


    for (int i = 0; i < ng; i++) {//child wair
        waitpid(gpids[i], NULL, 0);
    }

    // Send termination message to moderator
    // if (msgctl(ag_qid, IPC_RMID, NULL) == -1 && errno != EINVAL) {
    //     perror("Error removing app-group message queue");
    //     return 1;
    // }

    return 0;
}

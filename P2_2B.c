// p2b.c - SYSC4001 A3 Part 2(b): TA marking with shared memory + semaphores

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <semaphore.h>

#define MAX_EXAMS  64
#define NUM_Q      5

typedef struct {

    char rubric[NUM_Q];
    int exam_student[MAX_EXAMS];
    int total_exams;
    int current_exam_index;     
    int current_student;        
    int question_state[NUM_Q];
    int all_done;

    sem_t exam_mutex;      // protects current_exam_index, current_student, question_state reset
    sem_t rubric_mutex;    // protects rubric changes + rubric.txt writes
    sem_t question_mutex;  // protects question_state[] accesses
} shared_t;


void random_sleep(int min_ms, int max_ms) {
    int delta = max_ms - min_ms;
    int r = rand() % (delta + 1);
    int ms = min_ms + r;
    nanosleep(ms * 1000);
}

// Load rubric.txt: 5 lines 
void load_rubric(shared_t *sh) {
    FILE *f = fopen("rubric.txt", "r");
    if (!f) {
        perror("rubric.txt");
        exit(1);
    }
    for (int i = 0; i < NUM_Q; i++) {
        int num;
        char comma, letter;
        if (fscanf(f, "%d %c %c", &num, &comma, &letter) != 3) {
            fprintf(stderr, "Bad rubric line %d\n", i + 1);
            fclose(f);
            exit(1);
        }
        sh->rubric[i] = letter;
    }
    fclose(f);
}

// Save rubric to rubric.txt
void save_rubric(shared_t *sh) {
    FILE *f = fopen("rubric.txt", "w");
    if (!f) {
        perror("rubric.txt");
        return;
    }
    for (int i = 0; i < NUM_Q; i++) {
        fprintf(f, "%d, %c\n", i + 1, sh->rubric[i]);
    }
    fclose(f);
}

// Load exam_list.txt
int load_exam_list(shared_t *sh) {
    FILE *f = fopen("exam_list.txt", "r");
    if (!f) {
        perror("exam_list.txt");
        exit(1);
    }
    int snum, count = 0;
    while (count < MAX_EXAMS && fscanf(f, "%d", &snum) == 1) {
        sh->exam_student[count++] = snum;
    }
    fclose(f);
    sh->total_exams = count;
    return count;
}

// Load a specific exam into shared memory
void load_exam_into_shared(shared_t *sh, int idx) {
    if (idx < 0 || idx >= sh->total_exams) {
        sh->all_done = 1;
        return;
    }
    int snum = sh->exam_student[idx];
    sh->current_student = snum;
    for (int i = 0; i < NUM_Q; i++) {
        sh->question_state[i] = 0;
    }
    printf("[COORD] Loaded exam index %d student %04d\n", idx, snum);
    fflush(stdout);
}

// TA process 
void ta_process(shared_t *sh, int ta_id) {
    srand(getpid()); 

    while (1) {
        if (sh->all_done) {
            printf("TA %d (PID %d): all_done set, exiting.\n", ta_id, getpid());
            fflush(stdout);
            exit(0);
        }

        int need_new_exam = 0;

        // Check question_state[] under question_mutex
        sem_wait(&sh->question_mutex);
        int all_marked = 1;
        for (int i = 0; i < NUM_Q; i++) {
            if (sh->question_state[i] != 2) {
                all_marked = 0;
                break;
            }
        }
        if (sh->current_student == 0 || all_marked) {
            need_new_exam = 1;
        }
        sem_post(&sh->question_mutex);

        if (need_new_exam) {
            // Only one TA can move on to the next exam at a time
            sem_wait(&sh->exam_mutex);

            if (!sh->all_done) {
                int idx = ++sh->current_exam_index;
                if (idx >= sh->total_exams) {
                    sh->all_done = 1;
                    sem_post(&sh->exam_mutex);
                    continue;
                }

                load_exam_into_shared(sh, idx);
                int student = sh->current_student;

                if (student == 9999) {
                    printf("TA %d (PID %d): reached student 9999, setting all_done.\n",
                           ta_id, getpid());
                    sh->all_done = 1;
                }
            }

            sem_post(&sh->exam_mutex);
            continue;
        }

        int student = sh->current_student;
        printf("TA %d (PID %d): reviewing rubric for student %04d\n",
               ta_id, getpid(), student);
        fflush(stdout);

        // exclusive lock
        sem_wait(&sh->rubric_mutex);
        for (int q = 0; q < NUM_Q; q++) {
            random_sleep(500, 1000); 
            int change = rand() % 4 == 0; 
            if (change) {
                char old = sh->rubric[q];
                sh->rubric[q] = old + 1;
                printf("TA %d (PID %d): corrected rubric Q%d %c -> %c\n",
                       ta_id, getpid(), q + 1, old, sh->rubric[q]);
                fflush(stdout);
                save_rubric(sh);
            }
        }
        sem_post(&sh->rubric_mutex);

        while (1) {
            // Check  all questions
            sem_wait(&sh->question_mutex);
            int q_done = 1;
            for (int q = 0; q < NUM_Q; q++) {
                if (sh->question_state[q] != 2) {
                    q_done = 0;
                    break;
                }
            }
            sem_post(&sh->question_mutex);

            if (q_done) break;

            // Look for an unmarked question
            int q_to_mark = -1;
            sem_wait(&sh->question_mutex);
            for (int q = 0; q < NUM_Q; q++) {
                if (sh->question_state[q] == 0) {
                    sh->question_state[q] = 1; // being marked
                    q_to_mark = q;
                    break;
                }
            }
            sem_post(&sh->question_mutex);

            if (q_to_mark == -1) {
                nanosleep(100 * 1000);
                continue;
            }

            printf("TA %d (PID %d): marking student %04d question %d\n",
                   ta_id, getpid(), student, q_to_mark + 1);
            fflush(stdout);

            random_sleep(1000, 2000); 


            sem_wait(&sh->question_mutex);
            sh->question_state[q_to_mark] = 2;
            sem_post(&sh->question_mutex);
        }

        printf("TA %d (PID %d): finished exam for student %04d\n",
               ta_id, getpid(), student);
        fflush(stdout);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <num_TAs>\n", argv[0]);
        return 1;
    }
    int num_TAs = atoi(argv[1]);
    if (num_TAs < 2) {
        fprintf(stderr, "num_TAs must be >= 2\n");
        return 1;
    }

    key_t key = ftok(".", 'R');
    int shmid = shmget(key, sizeof(shared_t), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        return 1;
    }

    shared_t *sh = (shared_t *)shmat(shmid, NULL, 0);
    if (sh == (void *)-1) {
        perror("shmat");
        return 1;
    }


    memset(sh, 0, sizeof(*sh));
    load_rubric(sh);
    load_exam_list(sh);
    sh->current_exam_index = -1; 
    sh->all_done = 0;

    // Initialize semaphores 
    if (sem_init(&sh->exam_mutex, 1, 1) == -1) {
        perror("sem_init exam_mutex");
        exit(1);
    }
    if (sem_init(&sh->rubric_mutex, 1, 1) == -1) {
        perror("sem_init rubric_mutex");
        exit(1);
    }
    if (sem_init(&sh->question_mutex, 1, 1) == -1) {
        perror("sem_init question_mutex");
        exit(1);
    }

    // Fork TA processes
    for (int i = 0; i < num_TAs; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            ta_process(sh, i);
            exit(0);
        } else if (pid < 0) {
            perror("fork");
        }
    }


    for (int i = 0; i < num_TAs; i++) {
        wait(NULL);
    }


    sem_destroy(&sh->exam_mutex);
    sem_destroy(&sh->rubric_mutex);
    sem_destroy(&sh->question_mutex);


    shmdt(sh);
    shmctl(shmid, IPC_RMID, NULL);
    return 0;
}

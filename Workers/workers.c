#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_QUEUE_NAME 256
#define TASK_QUEUE_NAME "/task_queue_%d"
#define RESULT_QUEUE_NAME "/result_queue_%d_%d"

#define MAX_MSG_SIZE 10  // Max message size
#define MAX_MSGS 10      // Queue size

#define MIN_WORKERS 2
#define MAX_WORKERS 20
#define MIN_TIME 100
#define MAX_TIME 5000
#define UNUSED(x) ((void)(x))

#define MIN_SLEEP_TIME 500
#define MAX_SLEEP_TIME 2000
volatile sig_atomic_t last_signal = 0;
volatile sig_atomic_t children_left;

typedef struct msg
{
    float x;
    float y;
} msg_t;

typedef struct msg2
{
    pid_t pid;
    float result;
} msg2_t;

void usage(const char *name);
void sigchld_handler(int sig);
int sethandler(void (*f)(int), int sigNo);
void sig_handler(int sig);
void thread_func(union sigval sv);
void child_work(mqd_t ptc, pid_t server_pid);
void parent_work(int T1, int T2, mqd_t ptc);
pid_t *create_children(int N, struct mq_attr *attr, char *mq_name);


int main(int argc, char **argv)
{
    if (argc != 4)
        usage(argv[0]);

    int N = atoi(argv[1]);
    if (N < 2 || N > 20)
        usage(argv[0]);

    int T1 = atoi(argv[2]);
    int T2 = atoi(argv[3]);
    if (T1 < MIN_TIME || T2 > MAX_TIME || T1 >= T2)
        usage(argv[0]);

    if(sethandler(sigchld_handler, SIGCHLD))
        ERR("Setting parent SIGCHLD:");
    if (sethandler(sig_handler, SIGINT))
        ERR("Setting SIGINT handler");

    children_left = N;
    
    mqd_t ptc;
    struct mq_attr attr;
    attr.mq_maxmsg = MAX_MSGS;
    attr.mq_msgsize = MAX_MSG_SIZE;
    char mq_name[MAX_QUEUE_NAME] = {0};
    char mq_result_name[MAX_QUEUE_NAME] = {0};

    printf("Server is starting...\n");

    snprintf(mq_name, MAX_QUEUE_NAME, TASK_QUEUE_NAME, getpid());
    pid_t *children_pid = create_children(N, &attr, mq_name);

    mqd_t *ctp = (mqd_t *)malloc(N * sizeof(mqd_t));
    if (!ctp)
        ERR("malloc");

    static struct sigevent noti;
    noti.sigev_notify = SIGEV_THREAD;
    noti.sigev_notify_function = thread_func;

    for (int i = 0; i < N; i++)
    {
        snprintf(mq_result_name, MAX_QUEUE_NAME, RESULT_QUEUE_NAME, getpid(), children_pid[i]);
        if ((ctp[i] = TEMP_FAILURE_RETRY(mq_open(mq_result_name, O_RDONLY | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
            ERR("mq open server");

        noti.sigev_value.sival_ptr = &(ctp[i]);

        if (mq_notify(ctp[i], &noti) < 0)
            ERR("mq_notify");
    }

    if ((ptc = TEMP_FAILURE_RETRY(mq_open(mq_name, O_WRONLY | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq_open server");

    parent_work(T1, T2, ptc);
    mq_close(ptc);

    if (mq_unlink(mq_name))
        ERR("mq unlink");

    while (wait(NULL) > 0)
        ;

    for (int i = 0; i < N; i++)
        mq_close(ctp[i]);

    free(ctp);
    free(children_pid);


    printf("All child processes have finished.\n");
    return EXIT_SUCCESS;
}


void usage(const char *name)
{
    fprintf(stderr, "USAGE: %s N T1 T2\n", name);
    fprintf(stderr, "N: %d <= N <= %d - number of workers\n", MIN_WORKERS, MAX_WORKERS);
    fprintf(stderr, "T1, T2: %d <= T1 < T2 <= %d - time range for spawning new tasks\n", MIN_TIME, MAX_TIME);
    exit(EXIT_FAILURE);
}


void sigchld_handler(int sig)
{
    UNUSED(sig);
    pid_t pid;
    for (;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (0 == pid)
            return;
        if (0 >= pid)
        {
            if (ECHILD == errno)
                return;
            ERR("waitpid:");
        }
        --children_left;
    }
}

int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}
void sig_handler(int sig)
{
    last_signal = sig;
}

void thread_func(union sigval sv)
{
    mqd_t *ctp = (mqd_t *)sv.sival_ptr;
    static struct sigevent sev;
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = ctp;
    sev.sigev_notify_attributes = NULL;
    sev.sigev_notify_function = thread_func;
    if (mq_notify(*ctp, &sev) < 0)
        ERR("mq_notify");
    char buf[MAX_MSG_SIZE];
    msg2_t *msg;

    for (;;)
    {
        if (mq_receive(*ctp, buf, MAX_MSG_SIZE, NULL) < (ssize_t) sizeof(msg2_t))
        {
            if (errno == EAGAIN)
                break;
            else
                ERR("mq_receive");
        }
        msg = (msg2_t *)buf;
        printf("Result from worker %d: %.1f\n", msg->pid, msg->result);
    }
}

void child_work(mqd_t ptc, pid_t server_pid)
{
    unsigned int message_priority;
    mqd_t ctp;
    struct mq_attr attr;
    attr.mq_maxmsg = MAX_MSGS;
    attr.mq_msgsize = MAX_MSG_SIZE;
    char mq_name[MAX_QUEUE_NAME] = {0};
    snprintf(mq_name, MAX_QUEUE_NAME, RESULT_QUEUE_NAME, server_pid, getpid());
    if ((ctp = TEMP_FAILURE_RETRY(mq_open(mq_name, O_WRONLY | O_NONBLOCK | O_CREAT, 0600, &attr))) == (mqd_t)-1)
        ERR("mq open server");

    char buf[MAX_MSG_SIZE];
    msg_t *msg;
    msg2_t msg2;
    msg2.pid = getpid();
    printf("[%d] Worker ready!\n", getpid());
    unsigned int sleep_time;
    unsigned int seconds;
    unsigned int milliseconds;
    struct timespec ts;

    for(;;)
    {
        if (TEMP_FAILURE_RETRY(mq_receive(ptc, buf, MAX_MSG_SIZE, &message_priority)) < (ssize_t) sizeof(msg_t))
            ERR("mq_receive");

        if(message_priority == 1)
            break;

        msg = (msg_t *)buf;
        printf("[%d] Received task [%.1f, %.1f]\n", getpid(), msg->x, msg->y);
        sleep_time = rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME) + MIN_SLEEP_TIME;
        seconds = sleep_time / 1000;
        milliseconds = (sleep_time - seconds * 1000);
        ts.tv_sec = seconds;
        ts.tv_nsec = milliseconds * 1000000;
        nanosleep(&ts, NULL);

        msg2.result = msg->x + msg->y;

        if (TEMP_FAILURE_RETRY(mq_send(ctp, (const char *)&msg2, sizeof(msg2_t), 0)))
            ERR("mq_send");

        printf("[%d] Result sent [%.1f]\n", msg2.pid, msg2.result);
    }

    mq_close(ctp);
    if (mq_unlink(mq_name))
        ERR("mq unlink");

    printf("[%d] Exits!\n", getpid());
}
void parent_work(int T1, int T2, mqd_t ptc)
{
    msg_t msg;
    unsigned int sleep_time;
    unsigned int seconds;
    unsigned int milliseconds;
    struct timespec ts;

    while(last_signal != SIGINT)
    {
        sleep_time = rand() % (T2 - T1) + T1;
        seconds = sleep_time / 1000;
        milliseconds = (sleep_time - seconds * 1000);
        ts.tv_sec = seconds;
        ts.tv_nsec = milliseconds * 1000000;
        nanosleep(&ts, NULL);
        msg.x = (float)rand() / (float)(RAND_MAX / 100);
        msg.y = (float)rand() / (float)(RAND_MAX / 100);
        if (TEMP_FAILURE_RETRY(mq_send(ptc, (const char *)&msg, sizeof(msg_t), 0)))
        {
            if (errno == EAGAIN)
                printf("Queue is full!\n");
            else
                ERR("mq_send");
        }
        else
            printf("New task queued: [%.1f, %.1f]\n", msg.x, msg.y);
    }
    msg.x = 0;
    msg.y = 0;
    while(children_left)
    {
        if (TEMP_FAILURE_RETRY(mq_send(ptc, (const char*) &msg, sizeof(msg_t), 1)))
        {
            if (errno != EAGAIN)
                ERR("mq_send");
        }
    }

}

pid_t *create_children(int N, struct mq_attr *attr, char *mq_name)
{
    pid_t server_pid = getpid();
    pid_t child_pid;
    mqd_t ptc;

    pid_t *children_pid = (pid_t *)malloc(N * sizeof(pid_t));
    if (!children_pid)
        ERR("malloc");

    for (int i = 0; i < N; i++)
    {
        switch (child_pid = fork())
        {
            case 0:
                free(children_pid);
                if ((ptc = TEMP_FAILURE_RETRY(mq_open(mq_name, O_RDONLY | O_CREAT, 0600, attr))) == (mqd_t)-1)
                    ERR("mq open server");

                child_work(ptc, server_pid);

                mq_close(ptc);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("Fork:");
            default:
                children_pid[i] = child_pid;
        }
    }

    return children_pid;
}

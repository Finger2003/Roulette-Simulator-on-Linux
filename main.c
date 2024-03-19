#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define UNUSED(x) ((void)(x))

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))
#define MAX_RANDOM 36
#define BUF_SIZE 3
#define WIN_MULTIPLIER 35

void usage(char* name);
int sethandler(void (*f)(int), int sigNo);
void sigchld_handler(int sig);
void child_work(int M, int r, int w);
void parent_work(int N, int* ctp, int* ptc);
void create_children(int N, int M, int* ctp, int* ptc);

int main(int argc, char** argv)
{
    if (argc != 3)
        usage(argv[0]);

    int N = atoi(argv[1]);
    int M = atoi(argv[2]);
    if (N < 1 || M < 100)
        usage(argv[0]);

    if (sethandler(sigchld_handler, SIGCHLD))
        ERR("Setting parent SIGCHLD:");

    int* ctp = malloc(N * sizeof(int));
    if (!ctp)
        ERR("malloc:");
    int* ptc = malloc(N * sizeof(int));
    if (!ptc)
        ERR("malloc:");
    create_children(N, M, ctp, ptc);
    parent_work(N, ctp, ptc);

    free(ctp);
    free(ptc);
    while (wait(NULL) > 0)
        ;
    return 0;
}

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s N M\n", name);
    fprintf(stderr, "N: N >= 1 - number of players\n");
    fprintf(stderr, "M: M >= 100 - initial amount of money\n");
    exit(EXIT_FAILURE);
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
    }
}

void child_work(int M, int r, int w)
{
    srand(getpid());
    printf("[%d]: I have %d$ and I'm going to play roulette\n", getpid(), M);

    int buf[BUF_SIZE];
    int pid = getpid();
    int amount;
    int number;
    int lucky_number;
    while (M > 0)
    {
        if ((rand() % 10) == 0)
        {
            printf("[%d]: I saved %d$\n", pid, M);
            return;
        }
        amount = rand() % M + 1;
        number = rand() % (MAX_RANDOM + 1);
        buf[0] = pid;
        buf[1] = amount;
        buf[2] = number;
        if (write(w, &buf, sizeof(buf)) < 0)
            ERR("write to parent");
        if (TEMP_FAILURE_RETRY(read(r, &lucky_number, sizeof(int))) < 0)
            ERR("read from parent");
        if (number == lucky_number)
        {
            int win = WIN_MULTIPLIER*amount;
            M += win;
            printf("[%d]: Whoa, I won %d$\n", pid, win);
        }

            M -= amount;
    }
    printf("[%d]: I'm broke\n", pid);
}

void parent_work(int N, int* ctp, int* ptc)
{
    int* buf = malloc(N * BUF_SIZE * sizeof( int));
    if (!buf)
        ERR("malloc:");
    int status;
    int lucky_number;
    srand(getpid());
    int active_players = N;
    while (active_players > 0)
    {
        for (int i = 0; i < N; i++)
        {
            if (ctp[i])
            {
                status = TEMP_FAILURE_RETRY(read(ctp[i], buf + BUF_SIZE * i, BUF_SIZE * sizeof(int)));
                if (status < 0)
                    ERR("TEMP_FAILURE_RETRY(read header from child");
                if (status == 0)
                {
                    if (ctp[i] && close(ctp[i]))
                        ERR("close");
                    if (ptc[i] && close(ptc[i]))
                        ERR("close");
                    ctp[i] = 0;
                    ptc[i] = 0;
                    --active_players;
                }
            }
        }
        lucky_number = rand() % (MAX_RANDOM + 1);
        for (int i = 0; i < N; i++)
        {
            if (ptc[i])
            {
                printf("Croupier: [%d]: placed %d on a %d\n", buf[BUF_SIZE * i], buf[BUF_SIZE * i + 1],
                       buf[BUF_SIZE * i + 2]);
                if (write(ptc[i], &lucky_number, sizeof(int)) < 0)
                    ERR("write to child");
            }
        }
        printf("Croupier: %d is the lucky number\n", lucky_number);
    }
    free(buf);
    printf("Croupier: Casino always wins\n");
}

void create_children(int N, int M, int* ctp, int* ptc)
{
    int tmp_ctp[2];
    int tmp_ptc[2];
    for (int i = 0; i < N; i++)
    {
        if (pipe(tmp_ctp) || pipe(tmp_ptc))
            ERR("pipe");
        switch (fork())
        {
            case 0:
                for (int j = 0; j < i; j++)
                {
                    if (ctp[j] && close(ctp[j]))
                        ERR("close");
                    if (ptc[j] && close(ptc[j]))
                        ERR("close");
                }
                free(ctp);
                free(ptc);
                if (close(tmp_ptc[1]))
                    ERR("close");
                if (close(tmp_ctp[0]))
                    ERR("close");

                child_work(M, tmp_ptc[0], tmp_ctp[1]);
                if (close(tmp_ptc[0]))
                    ERR("close");
                if (close(tmp_ctp[1]))
                    ERR("close");
                exit(EXIT_SUCCESS);
            case -1:
                ERR("Fork:");
        }
        if (close(tmp_ctp[1]))
            ERR("close");
        if (close(tmp_ptc[0]))
            ERR("close");
        ctp[i] = tmp_ctp[0];
        ptc[i] = tmp_ptc[1];
    }
}
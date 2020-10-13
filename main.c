#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#ifndef ITER_CNT_CHECK
#define ITER_CNT_CHECK (size_t)(2 * 1e8)
#endif

#define ERROR_CODE -1
#define SUCCESS_CODE 0
#define MIN_ARG_CNT 2

#ifndef ITER_FAULT
#define ITER_FAULT (size_t)(2 * 1e8)
#endif

void exitWithFailure(const char *msg, int errcode){
    errno = errcode;
    fprintf(stderr, "%.256s:%.256s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

char is_intr = 0;

void sigcatch(){
    is_intr = 1;
}

/*
    struct for passing arguments
    to the thread
*/
typedef struct Context{
    size_t  thread_cnt,
            /* thread identifier */
            thread_id;
    double *res;
} Context;

double addendum(size_t ind){
    double denum = (ind) * 2.0 + 1.0;
        if (ind % 2 == 1)
            denum *= -1.0;
    
    return 1.0 / denum;
}

void *routine(void *data){
    Context *cntx = ((Context*)(data));

    size_t ind = cntx->thread_id;
    double sum = 0.0;

    /*
        each thread uses (thread_id + k * thread_cnt)th
        numbers of row
    */

    size_t iter_cnt = 0;
    size_t iter_cnt_check = 0;
    while(1){
        if (iter_cnt_check == ITER_CNT_CHECK){
            if (is_intr){
                /* round up */
                size_t final_iter = ((iter_cnt + ITER_FAULT - 1) / ITER_FAULT) * ITER_FAULT;
                
                for (size_t i = iter_cnt; i < final_iter; ++i){
                    sum += addendum(ind);
                    ind += cntx->thread_cnt;
                }

                *(cntx->res) = sum;
                pthread_exit((void*)SUCCESS_CODE);
            }

            iter_cnt_check = 0;
        }

        sum += addendum(ind);
        ind += cntx->thread_cnt;
        ++iter_cnt;
        ++iter_cnt_check;
    }
}

int main(int argc, char **argv){
    char *endptr;
    errno = SUCCESS_CODE;
    const size_t thread_cnt = argc >= MIN_ARG_CNT ? strtol(argv[1], &endptr, 10) : 4;
    if (errno != SUCCESS_CODE)
        exitWithFailure("main", EINVAL);

    errno = SUCCESS_CODE;
    signal(SIGINT, sigcatch);
    if (errno != SUCCESS_CODE)
        exitWithFailure("main:", errno);
    
    pthread_t *pid;
    Context *cntx;

    pid = (pthread_t*)malloc(sizeof(pthread_t) * thread_cnt);
    if (pid == NULL)
        exitWithFailure("main", ENOMEM);
    cntx = (Context*)malloc(sizeof(Context) * thread_cnt);
    if (cntx == NULL)
        exitWithFailure("main", ENOMEM);

    for (int i = 0; i < thread_cnt; ++i){
        cntx[i].thread_cnt = thread_cnt;
        cntx[i].thread_id = i;
        cntx[i].res = (double*)malloc(sizeof(double));
        if (cntx[i].res == NULL)
            exitWithFailure("main", ENOMEM);

        int err = pthread_create(&pid[i], NULL, routine, (void*)(&cntx[i]));
        if (err != SUCCESS_CODE)
            exitWithFailure("main", err);
    }

    double sum = 0.0;
    for (int i = 0; i < thread_cnt; ++i){
        /* wait for threads to terminate */
        int err = pthread_join(pid[i], NULL);
        if (err != SUCCESS_CODE)
            exitWithFailure("main", err);

        sum += *(cntx[i].res);
        /* release memory*/
        free(cntx[i].res);
    }

    /* release memory again */
    free(pid);
    free(cntx);

    sum *= 4.0;
    printf("%.10f\n", sum);

    return 0;
}

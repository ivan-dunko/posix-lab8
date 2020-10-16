#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#ifndef ITER_CNT_CHECK
#define ITER_CNT_CHECK (size_t)(1e3)
#endif

#define ERROR_CODE -1
#define SUCCESS_CODE 0
#define MIN_ARG_CNT 2

void exitWithFailure(const char *msg, int errcode){
    errno = errcode;
    fprintf(stderr, "%.256s:%.256s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

char is_intr = 0;
size_t *iter_cnt_arr = NULL;

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
    pthread_barrier_t *barrier;
} Context;

double addendum(size_t ind){
    double denum = (ind) * 2.0 + 1.0;
        if (ind % 2 == 1)
            denum *= -1.0;
    
    return 1.0 / denum;
}

size_t maxIterCntArray(size_t thread_cnt){
    size_t max_iter_cnt = 0;
    for (int i = 0; i < thread_cnt; ++i)
        if (iter_cnt_arr[i] > max_iter_cnt)
            max_iter_cnt = iter_cnt_arr[i];
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
                
                iter_cnt_arr[cntx->thread_id] = iter_cnt;
                pthread_barrier_wait(cntx->barrier);

                size_t max_iter_cnt = maxIterCntArray(cntx->thread_cnt);

                while (ind < max_iter_cnt){
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

int init(
    pthread_t **pid,
    Context **cntx,
    pthread_barrier_t *barrier,
    size_t thread_cnt){
    
    if (pid == NULL || cntx == NULL || barrier == NULL)
        return EINVAL;

    errno = SUCCESS_CODE;
    signal(SIGINT, sigcatch);
    if (errno != SUCCESS_CODE)
        exitWithFailure("main:", errno);

    *pid = (pthread_t*)malloc(sizeof(pthread_t) * thread_cnt);
    if (*pid == NULL)
        return ENOMEM;
    *cntx = (Context*)malloc(sizeof(Context) * thread_cnt);
    if (cntx == NULL)
        return ENOMEM;
    iter_cnt_arr = (size_t*)malloc(sizeof(size_t) * thread_cnt);
    if (iter_cnt_arr == NULL)
        return ENOMEM;
    int err = pthread_barrier_init(barrier, NULL, thread_cnt);
    if (err != SUCCESS_CODE)
        return err;

    for (int i = 0; i < thread_cnt; ++i){
        (*(cntx) + i)->thread_cnt = thread_cnt;
        (*(cntx) + i)->thread_id = i;
        (*(cntx) + i)->barrier = barrier;
        (*(cntx) + i)->res = (double*)malloc(sizeof(double));
        if ((*(cntx) + i)->res == NULL)
            return ENOMEM;

        int err = pthread_create(*pid + i, NULL, routine, (void*)(*cntx + i));
        if (err != SUCCESS_CODE)
            return err;
    }

    return SUCCESS_CODE;
}

double gatherPartialSums(
    pthread_t *pid, 
    Context *cntx, 
    size_t thread_cnt,
    double *result){

    if (pid == NULL || cntx == NULL || result == NULL)
        return EINVAL;

    double sum = 0.0;
    for (int i = 0; i < thread_cnt; ++i){
        /* wait for threads to terminate */
        int err = pthread_join(pid[i], NULL);
        if (err != SUCCESS_CODE)
            return err;

        sum += *(cntx[i].res);
        /* release memory*/
        free(cntx[i].res);
    }
    
    *result = sum;
    return SUCCESS_CODE;
}

int releaseResources(
    pthread_t *pid, 
    Context *cntx,
    pthread_barrier_t *barrier){
    
    if (pid == NULL || cntx == NULL || 
        iter_cnt_arr == NULL || barrier == NULL)
        return EINVAL;

    free(pid);
    free(cntx);
    free(iter_cnt_arr);
    int err = pthread_barrier_destroy(barrier);
    if (err != SUCCESS_CODE)
        return err;

    return SUCCESS_CODE;
}

int main(int argc, char **argv){
    char *endptr;
    errno = SUCCESS_CODE;
    const size_t thread_cnt = 
        argc >= MIN_ARG_CNT ? strtoll(argv[1], &endptr, 10) : 4;
    if (errno != SUCCESS_CODE || strtoll(argv[1], &endptr, 10) < 0)
        exitWithFailure("main", EINVAL);

    pthread_t *pid;
    Context *cntx;
    pthread_barrier_t barrier;

    int err = init(&pid, &cntx, &barrier, thread_cnt);
    if (err != SUCCESS_CODE)
        exitWithFailure("main", err);

    double sum;
    err = gatherPartialSums(pid, cntx, thread_cnt, &sum);
    if (err != SUCCESS_CODE)
        exitWithFailure("main", err);

    /* release memory */
    err = releaseResources(pid, cntx, &barrier);
    if (err != SUCCESS_CODE)
        exitWithFailure("main", err);

    printf("%.10f\n", sum * 4.0);
    return 0;
}

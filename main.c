#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>

#ifndef ITER_CNT_CHECK
#define ITER_CNT_CHECK (size_t)(1e3)
#endif

#define ERROR_CODE -1
#define SUCCESS_CODE 0
#define MIN_ARG_CNT 2
#define MAX_THREAD_CNT 5000

/*
    The maximum number of significant iterations:
        size_t overflow or double underflow
        are above this limit
*/
const size_t iter_cnt_limit = 
                sizeof(size_t) * 8 >= 53 ? ((size_t)1 << 53) : (size_t)(-1);

void exitWithFailure(const char *msg, int errcode){
    errno = errcode;
    fprintf(stderr, "%.256s:%.256s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

char is_intr = 0;

/*
    This flag is introduced to not to
    let happen situation where one thread
    pass by `if (is_intr)` block at `routine`
    and another one, after signal`s catched
    and `is_intr` is set to 1, pass in that block,
        thus resulting to deadlock.

    i. e. this flag implements
    sharing same value of `is_intr` flag
    among threads.
*/
char is_intr_detected = 0;

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
    int return_code;
} Context;

/*
    Returns ind`th addendum
    of sequence
*/
double addendum(size_t ind){
    double denum = (ind) * 2.0 + 1.0;
        if (ind % 2 == 1)
            denum *= -1.0;
    
    return 1.0 / denum;
}

/*
    Checks `pthread_barrier_wait` return code:
        in case it`s not successful
        this function stops current thread
*/
void assertBarrierWaitSuccess(Context *cntx, int err){
    if (err != SUCCESS_CODE && err != PTHREAD_BARRIER_SERIAL_THREAD){
        cntx->return_code = err;
        pthread_exit((void*)ERROR_CODE);
    }
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
        if (iter_cnt == iter_cnt_limit){
            *(cntx->res) = sum;
            pthread_exit((void*)SUCCESS_CODE);
        }

        if (iter_cnt_check == ITER_CNT_CHECK){
            if (is_intr)
                is_intr_detected = 1;
            /*
                this `pthread_barrier_wait` placed here to
                synchronize threads on number of made
                iterations ans properly set `is_intr_detected`
            */
            int err = pthread_barrier_wait(cntx->barrier);
            assertBarrierWaitSuccess(cntx, err);

            if (is_intr_detected){
                *(cntx->res) = sum;
                pthread_exit((void*)SUCCESS_CODE);
            }

            iter_cnt_check = 0;
            /*
                this `pthread_barrier_wait` placed here to
                not to let some thread set `is_intr_detected`
                to 1 if was set to 0. Consider example:

                1) is_intr_detected = 0 at upper barrier (A)
                2) first thread continues execution and makes
                    ITER_CNT_CHECK iterations
                3) `is_intr` is set to 1 and thus first thread
                    sets `is_intr_detected` to 1 and blocks
                4) second thread continues execution after (A)
                    and `is_intr_detected` is set to 1:
                        second threads stops execution -> deadlock
            */
            err = pthread_barrier_wait(cntx->barrier);
            assertBarrierWaitSuccess(cntx, err);
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
        return errno;

    *pid = (pthread_t*)malloc(sizeof(pthread_t) * thread_cnt);
    if (*pid == NULL)
        return ENOMEM;

    *cntx = (Context*)malloc(sizeof(Context) * thread_cnt);
    if (cntx == NULL)
        return ENOMEM;

    int err = pthread_barrier_init(barrier, NULL, thread_cnt);
    if (err != SUCCESS_CODE)
        return err;

    for (int i = 0; i < thread_cnt; ++i){
        (*(cntx) + i)->thread_cnt = thread_cnt;
        (*(cntx) + i)->thread_id = i;
        (*(cntx) + i)->barrier = barrier;
        (*(cntx) + i)->return_code = SUCCESS_CODE;
        (*(cntx) + i)->res = (double*)malloc(sizeof(double));
        if ((*(cntx) + i)->res == NULL)
            return ENOMEM;

        int err = pthread_create(*pid + i, NULL, routine, (void*)(*cntx + i));
        if (err != SUCCESS_CODE)
            return err;
    }

    return SUCCESS_CODE;
}

int gatherPartialSums(
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
        if (cntx->return_code != SUCCESS_CODE)
            return cntx->return_code;

        /*
            despite floating point arithmetics
            is not associative, since order of summation
            is the same all the time, result always will be
            the same for fixed number of iterations
        */
        sum += *(cntx[i].res);
    }
    
    *result = sum;
    return SUCCESS_CODE;
}

int releaseResources(
    pthread_t *pid, 
    Context *cntx,
    pthread_barrier_t *barrier){
    
    if (pid == NULL || cntx == NULL || 
        barrier == NULL)
        return EINVAL;

    size_t thread_cnt = cntx[0].thread_cnt;
    for (size_t i = 0; i < thread_cnt; ++i)
        free(cntx[i].res);

    free(pid);
    free(cntx);
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
    if (argc >= MIN_ARG_CNT 
        && (errno != SUCCESS_CODE || strtoll(argv[1], &endptr, 10) < 0))
        exitWithFailure("main", EINVAL);
        
    if (thread_cnt > MAX_THREAD_CNT)
        exitWithFailure("main: too much threads", EINVAL);
    
    pthread_t *pid;
    Context *cntx;
    pthread_barrier_t barrier;

    int err = init(&pid, &cntx, &barrier, thread_cnt);
    if (err != SUCCESS_CODE){
        releaseResources(pid, cntx, &barrier);
        exitWithFailure("main", err);
    }

    double sum;
    err = gatherPartialSums(pid, cntx, thread_cnt, &sum);
    if (err != SUCCESS_CODE){
        releaseResources(pid, cntx, &barrier);
        exitWithFailure("main", err);
    }

    /* release memory */
    err = releaseResources(pid, cntx, &barrier);
    if (err != SUCCESS_CODE)
        exitWithFailure("main", err);

    printf("%.10f\n", sum * 4.0);
    return 0;
}

#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "threadpool.h"

const int DEFAULT_TIME = 10;  //to test per 10s
const int MIN_WAIT_TASK_NUM = 10;  //queue_size > MIN_WAIT_TASK_NUM then add a new thread to thread pool
const int DEFAULT_THREAD_VARY = 10; // every you create or destroy the numbers of threads
const int true_ = 1;
const int false_ = 0;

typedef struct{
    void *(*function) (void *);  //call_back function
    void *arg;
} threadpool_task_t;    //

struct threadpool_t{
    pthread_mutex_t lock;    // to lock this struct
    pthread_mutex_t thread_counter;  //to record the num of busy threads
    pthread_cond_t queue_not_full;
    pthread_cont_t queue_not_empty;

    pthread_t *threads;   //save the every thread id in the pool
    pthread_t adjust_tid;  //management thread's id
    threadpool_task_t *task_queue; //task queue

    int min_thr_num;   //min threads number
    int max_thr_num;   // max threads number
    int live_thr_num;   //survival thread
    int busy_thr_num; 
    int wait_exit_thr_num;

    int queue_front;
    int queue_rear;
    int queue_size;    //task queue actual size
    int queue_max_size;

    int shutdown;   //the status of threadpool

}
void *threadpool_thread(void *threadpool)
{
    threadpool_t *pool = (threadpool_t *)threadpool;
    threadpool_task_t task;

    while(true){

        pthread_mutex_lock(&(pool->lock));  //wait until there is a task in the task queue(if not, then block )

        while ((pool->queue_size == 0) && (!pool->shutdown)){   //there isn't a task in the task queue
            printf("thread 0x%x is waiting\n", (unsigned int)pthread_self());
            pthread_cond_wait(&(pool->queue_not_empty), &(pool->lock));


            if (pool->wait_exit_thr_num > 0){
                pool->wait_exit_thr_num--;

                if (pool->live_thr_num > pool->min_thr_num){
                    printf("thread 0x%x is exiting\n", (unsigned int)pthread_self());
                    pool->live_thr_num--;
                    pthread_mutex_unlock(&(pool->lock));
                    pthread_exit(NULL);
                }
            }

            if (pool->shutdown){
                pthread_mutex_unlock(&(pool->lock));
                printf("thread 0x%x is exiting", (unsigned int)pthread_self());
                pthread_exit(NULL);

            }

            task.function = pool->task_queue[pool->queue_front].function;
            task.arg = pool->task_queue[pool->queue_front].arg;

            pool->queue_front = (pool->queue_front + 1) % pool->queue_max_size;
            pool->queue_size--;

            //to notify that a new task can join in
            pthread_cond_broadcast(&(pool->queue_not_full));  //add new task

            pthread_mutex_unlock(&(pool->lock));

            //excute task
            printf("thread 0x%x is working\n", (unsigned int)pthread_self());
            pthread_mutex_lock(&(pool->thread_counter));
            pool->busy_thr_num++;
            pthread_mutex_unlock(&(pool->thread_counter));
            (*(task.function)) (task.arg);


            printf("thread 0x%x end working\n",  (unsigned int)pthread_self());
            pthread_mutex_lock(&(pool->thread_counter));
            pool->busy_thr_num--;
            pthread_mutex_unlock(&(pool->thread_counter));


        }

        pthread_exit(NULL);
    }

}
threadpool_t *threadpool_create(int min_thr_num, int max_thr_num, int queue_max_size)
{
    int i;
    threadpool_t *pool = NULL;

    do{
        if ((pool = (threadpool_t *)malloc(sizeof(threadpool_t)) == NULL)){
            printf("malloc threadpool fail");
            break;
        }

        pool->min_thr_num = min_thr_num;
        pool->max_thr_num = max_thr_num;
        pool->busy_thr_num = 0;
        pool->live_thr_num = min_thr_num;
        pool->queue_size = 0;
        pool->queue_max_size = queue_max_size;
        pool->queue_front = 0;
        pool->queue_rear =0;
        pool->shutdown = false;

        pool->threads = (pthread_t *)malloc(sizeof(pthread_t)*max_thr_num);
        if (pool->threads == NULL){
            printf("malloc threads fail");
            break;
        }
        memset(pool->threads, 0, sizeof(pthread_t)*max_thr_num);

        pool->task_queue = (threadpool_task_t *)malloc(sizeof(threadpool_task_t)*queue_max_size;);
        if (pool->task_queue == NULL){
            printf("malloc task_queue fail");
            break;
        }

        if (pthread_mutex_init(&(pool->lock), NULL) != 0 
            || pthread_mutex_init(&(pool->pthread_counter), NULL) != 0
            || pthread_cond_init(&(pool->queue_not_empty), NULL) != 0
            || pthread_cond_init(&(pool->queue_not_full), NULL) != 0)
        {
            printf("init lock or cond fail");
            break;
        }

        for (i=0; i<min_thr_num; i++){
            pthread_create(&(pool->threads[i]), NULL, threadpool_thread, (void *)pool);
            printf("start thread 0x%x..\n"m (unsigned int)pool->thread[i]);
        }

        pthread_create(&(pool->adjust_tid), NULL, adjust_thread, (void *)pool);  //start management thread

        return pool;
    }while(0);

    threadpool_free(pool);   //when init fail, then free pool

    return NULL;
}
//add a task into threadpool
int threadpool_add(threadpool_t *pool, void*(*function) (void *arg), void *arg)
{
    pthread_mutex_lock(pool->lock);

    while ((pool->queue_size == pool->queue_max_size) && (!pool->shutdown)){
        pthread_cond_wait(&(pool->queue_not_full), &(pool->lock));
    }
    if (pool->shutdown){
        pthread_mutex_unlock(&(pool->lock));
    }
    //clear prar of callback function of work thread        
    if (pool->task_queue[pool->queue_rear].arg != NULL){
        free(pool->task_queue[pool->queue_rear].arg );
        pool->task_queue[pool->queue_rear].arg = NULL;
    }
    pool->task_queue[pool->queue_rear].function = function;
    pool->task_queue[pool->queue_rear].arg = arg;
    pool->queue_rear = (pool->queue_rear + 1) % pool->queue_max_size;
    pool->queue_size++;

    pthread_cond_signal(&(pool->queue_not_empty));
    pthread_mutex_unlock(&(pool->lock));

    return 0;

}

void *adjust_thread(void *threadpool)
{
    int i;
    threadpool_t *pool = (threadpool_t *)threadpool;
    while (!pool->shutdown){

        sleep(DEFAULT_TIME);

        pthread_mutex_lock(&(pool->lock));
        int queue_size = pool->queue_size;
        int live_thr_num = pool->live_thr_num;
        pthread_mutex_unlock(&(pool->lock));

        pthread_mutex_lock(&(pool->thread_count));
        int busy_thr_num = pool->busy_thr_num;
        pthread_mutex_unlock(&(pool->thread_count));


        //calculate whether to create or destroy threads
        if (queue_size >= MIN_WAIT_TASK_NUM && live_thr_num < pool->max_thr_num){
            pthread_mutex_lock(&(pool->lock));
            int add = 0;

            for (i = 0; i < pool->max_thr_num && add < DEFAULT_THREAD_VARY
                 && pool->live_thr_num < pool->max_thr_num; i++){

            }
        }


    }
}

//call_back function
void *process(void *arg){

    printf("thread 0x%x working on task %d\n", (unsigned int)pthread_self(), (int)arg);
    sleep(1); 
    printf("task %d is end\n", (int)arg);

    return NULL;
}
int main()
{

    threadpool_t *thp = threadpool_create(3,100,100);   //p1:minus thread's number p2:max thread's number p3:max queue size
    printf("pool init");
  
    int num[20], i;

    for (i=0; i<20; i++){
        num[i] = i;
        printf("add task %d\n", i);
        threadpool_add(thp, process, (void *)&num[i]);

        threadpool_destroy(thp);
    }
    return 0;
}

/**
 * thread_pool.c - Student implementation scaffold
 *
 * The public API and queue helpers are fixed. You still need to:
 * - define struct thread_pool
 * - FIFO task-queue operations
 * - choose the shared state and wake-up protocol your pool needs
 * - pool initialization and rollback on failure
 * - worker synchronization and lifecycle
 * - wait / destroy semantics
 *
 * You may add private static helpers in this file.
 */

#include <pthread.h>
#include <stdlib.h>
#include "task_queue.h"

struct thread_pool {
    pthread_t *threads;
    int worker_count;
    task_queue tasks;
    //互斥锁
    pthread_mutex_t mutex;
    //条件变量
    pthread_cond_t cond1;  //等待任务的条件变量
    pthread_cond_t cond2; //等待队列为空的条件变量
    int shutdown; //当为0时，所有worker要销毁，当为1时，所有worker要继续工作
    /*
     * TODO: add the shared state your design needs.
     *
     * A workable design must let submit(), worker threads, wait(), and destroy()
     * agree on:
     * - whether queued work exists,
     * - whether work is currently executing,
     * - whether shutdown has started,
     * - how sleepers are woken when those facts change.
     */
    int unused;
};

void task_queue_init(task_queue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

/* TODO A: append one task at the tail while preserving head/tail/count. */
void task_queue_push(task_queue *queue, task *new_task) {
    (void)queue;
    (void)new_task;
    if (queue->head == NULL)
    {
        queue->head = new_task;
        queue->tail = new_task;
        queue->count++;
        return;
    }
    queue->tail->next = new_task;
    queue->tail = queue->tail->next;
    queue->count++;
}

/* TODO A: pop one task from the head and repair tail when the queue becomes empty. */
task *task_queue_pop(task_queue *queue) {
    (void)queue;
    if (queue->head == NULL)
    {
        queue->tail = NULL;
        return NULL;
    }
    if (queue->head == queue->tail)
    {
        task *temp = queue->head;
        queue->head = NULL;
        queue->tail = NULL;
        queue->count--;
        return temp;

    }
    task *temp = queue->head;
    queue->head = queue->head->next;
    queue->count--;
    return temp;
}

void task_queue_clear(task_queue *queue) {
    task *next_task;

    while ((next_task = task_queue_pop(queue)) != NULL) {
        free(next_task);
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

/*
 * TODO B: implement the worker loop.
 *
 * Required behavior:
 * - wait while no task is available
 * - exit cleanly after shutdown
 * - do not hold the synchronization lock that protects shared state
 *   while running the task
 * - after shutdown starts, do not begin any new queued task
 * - maintain shared state so wait/destroy semantics work
 */
void *thread_pool_worker_main(void *pool_arg) {
    (void)pool_arg;
    if (pool_arg == NULL )
    {
        return NULL;
    }
    thread_pool *pool = (thread_pool *)pool_arg;
    while (1)
    {
        pthread_mutex_lock(&pool->mutex);
        while (pool->tasks.count == 0 && pool->shutdown == 1) {
            // pthread_cond_wait 会自动解锁并休眠，醒来时自动重新加锁
            pthread_cond_wait(&pool->cond1, &pool->mutex);
        }

        // 2. 如果收到关机信号，且队列已经被彻底掏空了，就可以安心下班了
        if (pool->shutdown == 0 ) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }
        pool->unused--;
        task *temp = task_queue_pop(&pool->tasks);
        pthread_mutex_unlock(&pool->mutex);
        if (temp != NULL)
        {
            
            temp->task_fn(temp->task_arg);  //执行任务
            free(temp);
        }
        
        pthread_mutex_lock(&pool->mutex);
        pool->unused++;
        if (pool->tasks.count == 0 && pool->unused == pool->worker_count)
        {
            pthread_cond_broadcast(&pool->cond2);
        }
        pthread_mutex_unlock(&pool->mutex);
    }
}

/*
 * TODO B: create the thread pool.
 *
 * You need:
 * - argument validation
 * - pool / thread-array allocation
 * - queue initialization
 * - initialization of the synchronization state your design needs
 * - worker creation
 * - rollback if any step fails after earlier steps succeeded
 */
thread_pool *thread_pool_create(int worker_count) {
    (void)worker_count;
    struct thread_pool *pool = malloc(sizeof(struct thread_pool));  //分配线程池结构体
    if (pool == NULL)
    {
        return NULL;  //失败
    }
    pool->worker_count = worker_count;
    pool->threads = malloc(worker_count * sizeof(pthread_t));
    if (pool->threads == NULL)
    {
        return NULL;  //失败
    }
    pool->shutdown = 1;
    pool->unused = worker_count;
    task_queue_init(&pool->tasks);
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond1, NULL);
    pthread_cond_init(&pool->cond2, NULL);
    for (int i = 0; i < worker_count; i++)
    {
        if (pthread_create(&pool->threads[i], NULL, thread_pool_worker_main, pool) != 0) //有序删除退出
        {
            pthread_mutex_lock(&pool->mutex);
            pool->shutdown = 0;
            pthread_mutex_unlock(&pool->mutex);
            pthread_cond_broadcast(&pool->cond1);
            for (int j = 0; j < i; j++)
            {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            pthread_mutex_destroy(&pool->mutex);
            pthread_cond_destroy(&pool->cond1);
            pthread_cond_destroy(&pool->cond2);
            free(pool);
            return NULL;
        }
    }
    return pool;
}

/*
 * TODO B: submit a task safely.
 *
 * Required behavior:
 * - reject invalid input
 * - reject a pool that is already shutting down
 * - wake a waiting worker after a successful push
 * - submission may happen from inside a running worker task
 */
int thread_pool_submit(thread_pool *pool, thread_pool_task_fn task_fn, void *task_arg) {
    (void)pool;
    (void)task_fn;
    (void)task_arg;
    if (pool == NULL || task_fn == NULL)
    {
        return -1;
    }
    pthread_mutex_lock(&pool->mutex);
    if (pool->shutdown == 0)
    {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }
    
    task *new_task = malloc(sizeof(task));
    if (new_task == NULL)
    {
        return -1;
    }
    new_task->task_fn = task_fn;
    new_task->task_arg = task_arg;
    new_task->next = NULL;
    
    task_queue_push(&pool->tasks, new_task);
    pthread_cond_signal(&pool->cond1);
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

/*
 * TODO C: block until the pool is idle.
 *
 * Idle means:
 * - the queue is empty
 * - no worker is still executing a task
 */
void thread_pool_wait(thread_pool *pool) 
{ 
    (void)pool; 
    if (pool == NULL)
    {
        return;
    }
    pthread_mutex_lock(&pool->mutex);
    while (pool->tasks.count != 0 || pool->unused != pool->worker_count)
    {
        pthread_cond_wait(&pool->cond2, &pool->mutex);
    }
    pthread_mutex_unlock(&pool->mutex);

}

/*
 * TODO D: destroy the pool.
 *
 * Required behavior:
 * - let already-started tasks finish
 * - drop not-yet-started tasks
 * - free resources after all workers exit
 */
void thread_pool_destroy(thread_pool *pool) {
    if (pool == NULL) {
        return;
    }
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 0;
    pthread_cond_broadcast(&pool->cond1);
    pthread_mutex_unlock(&pool->mutex);
    for (int i = 0; i < pool->worker_count; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }
    task_queue_clear(&pool->tasks);
    free(pool->threads);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond1);
    pthread_cond_destroy(&pool->cond2);
    free(pool);

    /* Keep an explicit reference so the starter still builds with -Werror. */
    (void)task_queue_clear;
}

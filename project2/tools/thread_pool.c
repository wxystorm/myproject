#include "tools/thread_pool.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct task {
    struct task *next;
    thread_pool_task_fn task_fn;
    void *task_arg;
} task;

typedef struct {
    task *head;
    task *tail;
    int count;
} task_queue;

struct thread_pool {
    pthread_t *threads;
    int worker_count;
    int active_workers;
    int shutting_down;
    task_queue tasks;
    pthread_mutex_t mutex;
    pthread_cond_t has_work;
    pthread_cond_t idle;
};

static void task_queue_init(task_queue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

static void task_queue_push(task_queue *queue, task *new_task) {
    new_task->next = NULL;
    if (!queue->tail) {
        queue->head = new_task;
        queue->tail = new_task;
    } else {
        queue->tail->next = new_task;
        queue->tail = new_task;
    }
    queue->count++;
}

static task *task_queue_pop(task_queue *queue) {
    task *front = queue->head;
    if (!front)
        return NULL;

    queue->head = front->next;
    if (!queue->head)
        queue->tail = NULL;
    queue->count--;
    front->next = NULL;
    return front;
}

static void task_queue_clear(task_queue *queue) {
    task *next_task;
    while ((next_task = task_queue_pop(queue)) != NULL)
        free(next_task);
}

static void *thread_pool_worker_main(void *arg) {
    thread_pool *pool = arg;

    for (;;) {
        pthread_mutex_lock(&pool->mutex);
        while (pool->tasks.count == 0 && !pool->shutting_down)
            pthread_cond_wait(&pool->has_work, &pool->mutex);

        if (pool->shutting_down) {
            pthread_mutex_unlock(&pool->mutex);
            return NULL;
        }

        task *work = task_queue_pop(&pool->tasks);
        pool->active_workers++;
        pthread_mutex_unlock(&pool->mutex);

        work->task_fn(work->task_arg);
        free(work);

        pthread_mutex_lock(&pool->mutex);
        pool->active_workers--;
        if (pool->tasks.count == 0 && pool->active_workers == 0)
            pthread_cond_broadcast(&pool->idle);
        pthread_mutex_unlock(&pool->mutex);
    }
}

thread_pool *thread_pool_create(int worker_count) {
    if (worker_count <= 0)
        return NULL;

    thread_pool *pool = malloc(sizeof(*pool));
    if (!pool)
        return NULL;

    pool->threads = malloc((size_t)worker_count * sizeof(*pool->threads));
    if (!pool->threads) {
        free(pool);
        return NULL;
    }

    pool->worker_count = worker_count;
    pool->active_workers = 0;
    pool->shutting_down = 0;
    task_queue_init(&pool->tasks);
    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->has_work, NULL);
    pthread_cond_init(&pool->idle, NULL);

    int created = 0;
    for (; created < worker_count; created++) {
        if (pthread_create(&pool->threads[created], NULL,
                           thread_pool_worker_main, pool) != 0)
            break;
    }

    if (created != worker_count) {
        pthread_mutex_lock(&pool->mutex);
        pool->shutting_down = 1;
        pthread_cond_broadcast(&pool->has_work);
        pthread_mutex_unlock(&pool->mutex);

        for (int i = 0; i < created; i++)
            pthread_join(pool->threads[i], NULL);

        free(pool->threads);
        pthread_mutex_destroy(&pool->mutex);
        pthread_cond_destroy(&pool->has_work);
        pthread_cond_destroy(&pool->idle);
        free(pool);
        return NULL;
    }

    return pool;
}

int thread_pool_submit(thread_pool *pool, thread_pool_task_fn task_fn, void *task_arg) {
    if (!pool || !task_fn)
        return -1;

    task *new_task = malloc(sizeof(*new_task));
    if (!new_task)
        return -1;

    new_task->task_fn = task_fn;
    new_task->task_arg = task_arg;
    new_task->next = NULL;

    pthread_mutex_lock(&pool->mutex);
    if (pool->shutting_down) {
        pthread_mutex_unlock(&pool->mutex);
        free(new_task);
        return -1;
    }

    task_queue_push(&pool->tasks, new_task);
    pthread_cond_signal(&pool->has_work);
    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

void thread_pool_wait(thread_pool *pool) {
    if (!pool)
        return;

    pthread_mutex_lock(&pool->mutex);
    while (pool->tasks.count != 0 || pool->active_workers != 0)
        pthread_cond_wait(&pool->idle, &pool->mutex);
    pthread_mutex_unlock(&pool->mutex);
}

void thread_pool_destroy(thread_pool *pool) {
    if (!pool)
        return;

    pthread_mutex_lock(&pool->mutex);
    pool->shutting_down = 1;
    pthread_cond_broadcast(&pool->has_work);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->worker_count; i++)
        pthread_join(pool->threads[i], NULL);

    task_queue_clear(&pool->tasks);
    free(pool->threads);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->has_work);
    pthread_cond_destroy(&pool->idle);
    free(pool);
}

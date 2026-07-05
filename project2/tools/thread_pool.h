#ifndef TOOLS_THREAD_POOL_H
#define TOOLS_THREAD_POOL_H

typedef struct thread_pool thread_pool;
typedef void (*thread_pool_task_fn)(void *task_arg);

thread_pool *thread_pool_create(int worker_count);
int thread_pool_submit(thread_pool *pool, thread_pool_task_fn task_fn, void *task_arg);
void thread_pool_wait(thread_pool *pool);
void thread_pool_destroy(thread_pool *pool);

#endif /* TOOLS_THREAD_POOL_H */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

struct thread_pool_attr {
	size_t pool_size;
	size_t queue_size;
	size_t resize_percent;
	struct timeval size_down;
};

typedef struct thread_pool_t thread_pool_t;
int thread_pool_destroy(thread_pool_t *tp);
thread_pool_t *thread_pool_create(const struct thread_pool_attr *tp_attr);
int thread_pool_add_task(thread_pool_t *tp, void *(*worker_routine)(void *), void *args);
int thread_pool_shutdown_now(thread_pool_t *tp);
int thread_pool_shutdown_graceful(thread_pool_t *tp);

#endif //THREAD_POOL_H

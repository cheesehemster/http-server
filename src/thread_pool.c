#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

#include "thread_pool.h"

#include <unistd.h>
#include <sys/errno.h>

#include "log.h"

int thread_pool_destroy(thread_pool_t *tp);

struct task {
	void *(*start_routine)(void *);
	void *args;
};

struct thread_args {
	thread_pool_t *tp;
};

struct thread_pool_t {
	bool volatile shutdown_requested;
	bool volatile shutdown_graceful_requested;
	atomic_uint open_connections;
	size_t amount_threads;
	size_t capacity;
	pthread_t *threads;
	size_t task_queue_size;
	struct task *task_queue;
	size_t write_index;
	size_t read_index;
	pthread_mutex_t queue_mutex;
	pthread_cond_t queue_cond;
};

// TODO: error checking and make this in another thread so it doesnt block the caller
int thread_pool_add_task(thread_pool_t *tp, void *(*worker_routine)(void *), void *args) {
	if (tp == NULL || worker_routine == NULL) {
		lprintf(ERROR, "null ptr arg");
		return -1;
	}
	if (args == NULL) {
		lprintf(WARN, "worker routine args is NULL");
	}
	{
		const int lock_stat = pthread_mutex_lock(&tp->queue_mutex);
		if (lock_stat != 0) {
			errno = lock_stat;
			sys_error_printf("pthread_mutex_lock failed");
			return -1;
		}
	}
	if (((tp->write_index + 1) % tp->task_queue_size) == tp->read_index) {
		lprintf(ERROR, "thread pool task queue full");
		return -1;
	}
	tp->task_queue[tp->write_index].start_routine = worker_routine;
	tp->task_queue[tp->write_index].args = args;
	tp->write_index = (tp->write_index + 1) % tp->task_queue_size;
	{
		const int unlock_stat = pthread_mutex_unlock(&tp->queue_mutex);
		if (unlock_stat != 0) {
			errno = unlock_stat;
			sys_error_printf("pthread_mutex_unlock failed");
			return -1;
		}
	}
	{
		const int signal_stat = pthread_cond_signal(&tp->queue_cond);
		if (signal_stat != 0) {
			errno = signal_stat;
			sys_error_printf("pthread_cond_signal failed");
			return -1;
		}
	}
	return 0;
}

int thread_pool_get_task(thread_pool_t *tp, struct task *buf) {
	if (tp->read_index == tp->write_index) { // queue empty
		return -1;
	}
	*buf = tp->task_queue[tp->read_index];
	tp->read_index = (tp->read_index + 1) % tp->task_queue_size;
	return 0;
}


void *worker_routine(void *args) {
	if (args == NULL) {
		lprintf(ERROR, "null ptr");
		return NULL;
	}
	thread_pool_t *targs = args;
	while (!targs->shutdown_requested) {
		{
			const int lock_stat = pthread_mutex_lock(&targs->queue_mutex);
			if (lock_stat != 0) {
				errno = lock_stat;
				sys_error_printf("pthread_mutex_lock failed");
				return NULL;
			}
		}
		struct task t;
		while (thread_pool_get_task(targs, &t) == -1) {
			if (targs->shutdown_graceful_requested) {
				pthread_mutex_unlock(&targs->queue_mutex);
				return NULL;
			}
			struct timespec time;
			time.tv_nsec = 100'000000; // 100ms
			time.tv_sec = 10;
			const int stat = pthread_cond_timedwait(&targs->queue_cond, &targs->queue_mutex, &time);
			if (stat != 0) {
				if (stat == ETIMEDOUT) {
					pthread_mutex_unlock(&targs->queue_mutex);
					goto next;
				}
				sys_error_printf("pthread_cond_wait failed");
				exit(1);
			}
		}
		pthread_mutex_unlock(&targs->queue_mutex);
		t.start_routine(t.args);
	next:
		;
	}
	return NULL;
}

// TODO: make it one attribute arg not list of args
thread_pool_t *thread_pool_create(const struct thread_pool_attr *tp_attr) {
	if (tp_attr == NULL) {
		lprintf(ERROR, "null ptr arg");
		return nullptr;
	}
	thread_pool_t *tp = malloc(sizeof(*tp));
	if (tp == NULL) {
		sys_error_printf("malloc failed");
		return nullptr;
	}
	tp->threads = malloc(sizeof(pthread_t) * tp_attr->pool_size);
	if (tp->threads == NULL) {
		sys_error_printf("malloc failed");
		return nullptr;
	}
	tp->task_queue = malloc(sizeof(struct task) * tp_attr->queue_size);
	if (tp->task_queue == NULL) {
		sys_error_printf("malloc failed");
		return nullptr;
	}
	tp->amount_threads = tp_attr->pool_size; // number of threads created
	tp->capacity = tp_attr->pool_size; // number of threads space for
	tp->write_index = 0;
	tp->read_index = 0;
	tp->shutdown_requested = false;
	tp->shutdown_graceful_requested = false;
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init(&tp->queue_mutex, &attr);
	pthread_cond_init(&tp->queue_cond, nullptr);
	for (size_t i = 0; i < tp_attr->pool_size; i++) {
		// TODO: cleanup
		const int create_stat = pthread_create(&tp->threads[i], nullptr, worker_routine, tp);
		if (create_stat != 0) {
			errno = create_stat;
			sys_error_printf("pthread_create failed");
			tp->shutdown_requested = true;
			thread_pool_destroy(tp);
			return nullptr;
		}
	}
	return tp;
}

int thread_pool_shutdown_graceful(thread_pool_t *tp) {
	tp->shutdown_graceful_requested = true;
	for (size_t i = 0; i < tp->amount_threads; i++) {
		pthread_join(tp->threads[i], nullptr);
	}
	return 0;
}

int thread_pool_shutdown_now(thread_pool_t *tp) {
	tp->shutdown_requested = true;
	for (size_t i = 0; i < tp->amount_threads; i++) {
		pthread_join(tp->threads[i], nullptr);
	}
	return 0;
}

int thread_pool_destroy(thread_pool_t *tp) {
	free(tp->threads);
	free(tp->task_queue);
	free(tp);
	return 0;
}

void *thread_routine([[maybe_unused]] void *vargs) {
	printf("test\n");
	return NULL;
}



/*
int main(void) {
	struct thread_pool_attr attr = {.pool_size = 10, .queue_size = 10000};
	thread_pool_t *tp = thread_pool_create(&attr);
	if (tp == NULL) {
		return 1;
	}
	for (int i = 0; i < 100; i++) {
		if (thread_pool_add_task(tp, thread_routine, nullptr) == -1) {
			thread_pool_destroy(tp);
			return 1;
		}
	}
	thread_pool_shutdown_graceful(tp);
	thread_pool_destroy(tp);
	printf("done\n");
	return 0;
}
*/
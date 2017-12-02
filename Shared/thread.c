#include "thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mlist.h>
#include <errno.h>
#include <signal.h>

struct thread {
	pthread_t id;
	pthread_t parent;
	thread_t *sender;
	bool active;
	void *(*fn)(void*);
	void *arg;
	sem_t *sem_sleep;
	sem_t *sem_send;
	sem_t *sem_recv;
	void *data;
};

static mlist_t *threads = NULL;

static void ptcheck(int ret);
static void default_signal_handler(int signal);
static void traverse_children(pthread_t parent, void *routine);
static void *kill_and_join(pthread_t tid);
static void *base_thread_routine(void *arg);
static void destroy_thread(thread_t *thread);

// ========== Funciones públicas ==========

void thread_init() {
	thread_signal_capture(SIGTERM, NULL);
	threads = mlist_create();
	thread_create(NULL, NULL); // hilo conductor
}

thread_t *thread_create(void *routine, void *arg) {
	thread_t *thread = malloc(sizeof(thread_t));
	thread->parent = pthread_self();
	thread->sender = NULL;
	thread->active = true;
	thread->fn = routine;
	thread->arg = arg;
	thread->sem_sleep = thread_sem_create(0);
	thread->sem_send = thread_sem_create(1);
	thread->sem_recv = thread_sem_create(0);
	mlist_insert(threads, 0, thread);

	if(routine != NULL) {
		pthread_t id;
		ptcheck(pthread_create(&id, NULL, base_thread_routine, thread));
		thread->id = id;
	} else {
		thread->id = thread->parent;
		thread->parent = -1;
	}
	return thread;
}

void thread_exit(void *retvalue) {
	pthread_exit(retvalue);
}

void thread_sleep(unsigned time) {
	struct timespec req;
	req.tv_sec = time / 1000;
	req.tv_nsec = (time % 1000) * 1.0e6;
	nanosleep(&req, NULL);
}

void thread_suspend() {
	thread_t *thread = thread_self();
	thread_sem_wait(thread->sem_sleep);
}

void thread_resume(thread_t *thread) {
	thread_sem_signal(thread->sem_sleep);
}

void thread_send(thread_t *thread, void *data) {
	if(thread == NULL || !thread->active) return;
	thread_sem_wait(thread->sem_send);
	thread->sender = thread_self();
	thread->data = data;
	thread_sem_signal(thread->sem_recv);
}

thread_t *thread_sender() {
	thread_t *self = thread_self();
	return self->sender;
}

void *thread_receive() {
	thread_t *thread = thread_self();
	if(!thread->active) return NULL;
	thread_sem_wait(thread->sem_recv);
	void *data = thread->data;
	thread_sem_signal(thread->sem_send);
	return data;
}

void thread_respond(void *data) {
	thread_send(thread_sender(), data);
}

void *thread_wait(thread_t *thread) {
	void *ret = NULL;
	pthread_join(thread->id, &ret);
	return ret;
}

void thread_waitall() {
	void routine(thread_t *thread) { thread_wait(thread); }
	traverse_children(pthread_self(), routine);
}

void *thread_kill(thread_t *thread) {
	if(thread == NULL) return NULL;
	traverse_children(thread->id, thread_kill);

	thread->active = false;
	return kill_and_join(thread->id);
}

void thread_killall() {
	traverse_children(pthread_self(), thread_kill);
}

bool thread_killed(thread_t *thread) {
	return !thread->active || pthread_kill(thread->id, 0) != 0;
}

bool thread_active() {
	if(threads == NULL) return true;
	return !thread_killed(thread_self());
}

thread_t *thread_self() {
	pthread_t self = pthread_self();
	thread_t* tid;
	bool cond(thread_t *elem) { return elem->id == self; }
	do{
		tid = mlist_find(threads,cond);
	} while(tid == NULL && (thread_sleep(500),1));
	return tid;
}

thread_t *thread_main() {
	return mlist_last(threads);
}

thread_t *thread_parent(thread_t *thread) {
	bool cond(thread_t *elem) { return elem->id == thread->parent; }
	return mlist_find(threads, cond);
}

void thread_signal_send(thread_t *thread, int signal) {
	pthread_kill(thread->id, signal);
}

void thread_signal_capture(int signal, void *routine) {
	struct sigaction sa;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_handler = routine != NULL ? routine : default_signal_handler;
	ptcheck(sigaction(signal, &sa, NULL));
}

mutex_t *thread_mutex_create() {
	mutex_t *mutex = malloc(sizeof(mutex_t));
	ptcheck(pthread_mutex_init(mutex, NULL));
	return mutex;
}

void thread_mutex_lock(mutex_t *mutex) {
	ptcheck(pthread_mutex_lock(mutex));
}

void thread_mutex_unlock(mutex_t *mutex) {
	ptcheck(pthread_mutex_unlock(mutex));
}

void thread_mutex_destroy(mutex_t *mutex) {
	ptcheck(pthread_mutex_destroy(mutex));
	free(mutex);
}

sem_t *thread_sem_create(unsigned value) {
	sem_t *sem = malloc(sizeof(sem_t));
	ptcheck(sem_init(sem, 0, value));
	return sem;
}

void thread_sem_wait(sem_t *sem) {
	ptcheck(sem_wait(sem));
}

void thread_sem_signal(sem_t *sem) {
	ptcheck(sem_post(sem));
}

void thread_sem_destroy(sem_t *sem) {
	ptcheck(sem_destroy(sem));
	free(sem);
}

void thread_term() {
	mlist_destroy(threads, destroy_thread);
}

// ========== Funciones privadas ==========

static void ptcheck(int ret) {
	if(ret != 0 && errno != EINTR) {
		fprintf(stderr, "Error de hilos: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void default_signal_handler(int signal) {
	signal++;
}

static void traverse_children(pthread_t parent, void *routine) {
	bool filter(thread_t *thread) {
		return thread->parent == parent;
	}
	mlist_t *children = mlist_filter(threads, filter);
	mlist_traverse(children, routine);
	mlist_destroy(children, NULL);
}

static void *kill_and_join(pthread_t tid) {
	void *ret = NULL;
	pthread_kill(tid, SIGTERM);
	pthread_join(tid, &ret);
	return ret;
}

static void cleanup_routine(void *arg) {
	thread_t *thread = arg;
	bool cond(thread_t *elem) { return elem->id == thread->id; }
	mlist_remove(threads, cond, destroy_thread);
}

static void *base_thread_routine(void *arg) {
	thread_t *thread = arg;
	pthread_cleanup_push(cleanup_routine, arg);
	void *ret = thread->fn(thread->arg);
	pthread_exit(ret);
	pthread_cleanup_pop(0);
}

static void destroy_thread(thread_t *thread) {
	thread_sem_destroy(thread->sem_sleep);
	thread_sem_destroy(thread->sem_send);
	thread_sem_destroy(thread->sem_recv);
	free(thread);
}


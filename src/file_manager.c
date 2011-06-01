#include <pthread.h>
#include <glib.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "file_manager.h"


#define SLEEP_TIME	60	/* 60 second sleep time. */


static pthread_t thread;
static pthread_mutex_t lock;
static short running;

static GSList *file_list;


static void delete_file(gpointer data) {
	char *path = (char *)data;
	unlink(path);
	free(path);
}

static void *thread_main() {
	while(running) {
		sleep(SLEEP_TIME);
		pthread_mutex_lock(&lock);
		g_slist_free_full(file_list, delete_file);
		pthread_mutex_unlock(&lock);

	}
	pthread_exit(NULL);
}

int file_manager_init() {
	running = 1;
	pthread_mutex_init(&lock, NULL);
	if(pthread_create(&thread, NULL, thread_main, NULL))
		return FAIL;
	return SUCCESS;
}

void file_manager_kill() {
	running = 0;
	pthread_kill(thread, SIGCONT);
	pthread_join(thread, NULL);
	pthread_mutex_destroy(&lock);
}

void add_file(const char *path) {
	pthread_mutex_lock(&lock);
	file_list = g_slist_prepend(file_list, g_strdup(path));
	pthread_mutex_unlock(&lock);
}


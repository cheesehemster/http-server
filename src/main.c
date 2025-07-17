#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/errno.h>

#include "server.h"
#include "log.h"

// this file handles the cli and process starting and stopping, it does not handle the server logic

#define FILE_EXISTS 1
#define FILE_ERROR (-1)
#define FILE_NOT_EXISTS 0
int file_exists(const char* path) {
	FILE* tmp_fp = fopen(path, "r");
	if (tmp_fp != NULL) {
		if (fclose(tmp_fp) == EOF) {
			sys_error_printf("fclose failed");
			return FILE_ERROR;
		}
		return FILE_EXISTS;
	}
	// opening a file that doesn't exist in read mode sets errno to ENOENT.
	// ENOENT = No such file or directory
	if (errno != ENOENT) {
		sys_error_printf("fopen failed");
		return FILE_ERROR;
	}
	return FILE_NOT_EXISTS;
}

int write_pid(const pid_t pid, const char* path) {
	if (path == NULL) {
		lprintf(ERROR, "path char * argument to write_pid is null");
		return -1;
	}
	FILE* fp = fopen(path, "w");
	if (fp == NULL) {
		sys_error_printf("fopen failed");
		return -1;
	}
	size_t bytes_written = fwrite(&pid, sizeof(pid), 1, fp);
	if (bytes_written == 0) {
		sys_error_printf("fwrite failed");
		return -1;
	}
	if (fclose(fp) == -1) {
		sys_error_printf("fclose failed");
		return -1;
	}
	return 0;
}

int start(void) {
	// chinook.pid only exists when the server is running, check if it exists
	{
		int stat = file_exists("/tmp/chinook.pid");
		if (stat == FILE_ERROR)
			return -1;
		if (stat == FILE_EXISTS) {
			printf("server already started!\n");
			return 0;
		}
	}
	const pid_t pid = fork();
	if (pid == -1) {
		sys_error_printf("fork failed");
		return -1;
	}
	// parent

	if (pid != 0) {
		if (write_pid(pid, "/tmp/chinook.pid") == -1) {
			kill(pid, SIGTERM);
			return -1;
		}
		waitpid(pid, nullptr, 0);
		return 0;
	}
	// child
	lprintf(LOG, "SERVER START");
	int status = run_server();
	lprintf(LOG, "SERVER STOP");
	if (remove("/tmp/chinook.pid") == -1) {
		sys_error_printf("remove failed");
		exit(EXIT_FAILURE);
	}
	if (status == -1) {
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
}

int stop(void) {
	FILE* fp = fopen("/tmp/chinook.pid", "r");
	if (fp == nullptr) {
		if (errno == ENOENT) {
			lprintf(ERROR, "server not running!");
			return -1;
		}
		sys_error_printf("fopen failed");
		return -1;
	}
	pid_t pid;
	size_t bytes_read = fread(&pid, sizeof(pid), 1, fp);
	if (bytes_read == 0) {
		if (ferror(fp)) {
			sys_error_printf("fread failed");
		} else {
			lprintf(ERROR, ".pid file exists but is empty, try starting server?");
		}
		if (fclose(fp) == EOF)
			sys_error_printf("fclose failed");
		return -1;
	}
	if (fclose(fp) == EOF) {
		sys_error_printf("fclose failed");
		return -1;
	}
	if (kill(pid, SIGTERM) == -1) {
		sys_error_printf("kill failed");
		if (errno == ESRCH) {
			lprintf(ERROR, "stop server failed, no such process, you might of stopped the process manually, continuing, will try deleting pid file");
			remove("/tmp/chinook.pid");
		} else {
			lprintf(ERROR, "stop server failed, try again 'chinook stop'");
			return -1;
		}
	}
	printf("stopped server successfully!\n");
	return 0;
}

int restart(void) {
	if (stop() == -1 || (sleep(1) && 0) || start() == -1)
		return -1;
	return 0;
}

int reset_log(void) {
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/chinook_log.txt", getenv("HOME"));
	FILE *fp = fopen(path, "w");
	if (fp == NULL) {
		sys_error_printf("fopen failed");
		return -1;
	}
	if (fclose(fp) == -1) {
		sys_error_printf("fclose failed");
		return -1;
	}
	return 0;
}

int main(int argc, char* argv[]) {
	if (argc != 2) {
		printf("usage: chinook [start|stop|restart]\n");
		return EXIT_FAILURE;
	}
	if (strcmp(argv[1], "help") == 0) {
		printf("usage: chinook [start|stop|restart]\n");
		return EXIT_SUCCESS;
	}
	if (strcmp(argv[1], "start") == 0)
		return start() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	if (strcmp(argv[1], "stop") == 0)
		return stop() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	if (strcmp(argv[1], "restart") == 0)
		return restart() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	if (strcmp(argv[1], "reset-log") == 0)
		return reset_log() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	printf("chinook: unknown command: '%s'. for help, 'chinook help'\n", argv[1]);
	return EXIT_FAILURE;
}

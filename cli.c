#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/errno.h>

#include "main.h"

// this file handles the cli and process starting and stopping, it does not handle the server logic

// add this later for better logging
int log_printf(const char*, ...); // writes to the log file and writes to stdout when log stdout mode is on
int debug_printf(const char*, ...); // writes to stdout when debug mode is on
int verbose_printf(const char*, ...); // writes to stdout when verbose mode is on

int stop();

// check if chinook.pid exists
// if it doesnt create it
// fork()
// put pid in chinook.pid

// 1: file exists
// 0: file doesn't exist
// -1: error
#define FILE_EXISTS 1
#define FILE_ERROR (-1)
#define FILE_NOT_EXISTS 0

int file_exists(const char* path) {
	FILE* tmp_fp = fopen(path, "r");
	if (tmp_fp != NULL) {
		if (fclose(tmp_fp) == EOF) {
			perror("fclose failed");
			return FILE_ERROR;
		}
		return FILE_EXISTS;
	}
	// opening a file that doesn't exist in read mode sets errno to ENOENT.
	// ENOENT = No such file or directory
	if (errno != ENOENT) {
		perror("fopen for file exists failed");
		return FILE_ERROR;
	}
	return FILE_NOT_EXISTS;
}

int write_pid(pid_t pid, const char* path) {
	FILE* fp = fopen(path, "w");
	if (fp == NULL) {
		perror("fopen failed");
		return -1;
	}
	size_t bytes_written = fwrite(&pid, sizeof(pid), 1, fp);
	if (bytes_written == 0) {
		perror("fwrite failed");
		return -1;
	}
	if (fclose(fp) == -1) {
		perror("fclose failed");
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
		perror("fork() failed");
		return -1;
	}
	// parent

	if (pid != 0) {
		if (write_pid(pid, "/tmp/chinook.pid") == -1) {
			kill(pid, SIGTERM);
			return -1;
		}
		waitpid(pid, NULL, 0);
		return 0;
	}
	// child
	int status = run_server();
	if (remove("/tmp/chinook.pid") == -1) {
		perror("remove failed");
		exit(EXIT_FAILURE);
	}
	if (status == -1) {
		exit(EXIT_FAILURE);
	}
	exit(EXIT_SUCCESS);
}

int stop(void) {
	FILE* fp = fopen("/tmp/chinook.pid", "r");
	if (fp == NULL) {
		if (errno == ENOENT) {
			fprintf(stderr, "server not running!\n");
			return -1;
		}
		perror("fopen failed");
		return -1;
	}
	pid_t pid;
	size_t bytes_read = fread(&pid, sizeof(pid), 1, fp);
	if (bytes_read == 0) {
		if (ferror(fp)) {
			perror("fread failed");
		} else {
			fprintf(stderr, ".pid file exists but is empty, try starting server?\n");
		}
		if (fclose(fp) == EOF)
			perror("fclose failed");
		return -1;
	}
	if (fclose(fp) == EOF) {
		perror("fclose failed");
		return -1;
	}
	printf("pid: %i\n", pid);
	if (kill(pid, SIGTERM) == -1) {
		perror("kill failed");
		if (errno == ESRCH) {
			fprintf(
				stderr,
				"stop server failed, no such process, you might of stopped the process manually,\ncontinuing, will try deleting pid file\n");
			remove("/tmp/chinook.pid");
		} else {
			fprintf(stderr, "stop server failed, try again 'chinook stop'\n");
			return -1;
		}
	}
	printf("stopped server successfully!\n");
	return 0;
}

int restart(void) {
	if (stop() == -1 || start() == -1)
		return -1;
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
	printf("chinook: unknown command: '%s'. for help, 'chinook help'\n", argv[1]);
	return EXIT_FAILURE;
}

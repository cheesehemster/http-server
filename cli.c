#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/errno.h>

// this file handles the cli and process starting and stopping, it does not handle the server logic

// add this later
int log_printf(const char *, ...); // writes to the log file and writes to stdout when log stdout mode is on
int debug_printf(const char *, ...); // writes to stdout when debug mode is on
int verbose_printf(const char *, ...); // writes to stdout when verbose mode is on

// check if chinook.pid exists
// if it doesnt create it
// fork()
// put pid in chinook.pid
int start(void) {
	// chinook.pid only exists when the server is running.
	// check if it exists
	{
		FILE *tmp_fp;
		if ((tmp_fp = fopen("/tmp/chinook.pid", "r"))) {
			printf("server already started!\n");
			if (fclose(tmp_fp) == EOF) {
				perror("fclose failed");
				return -1;
			}
			return 0;
		}
		// opening a file that doesn't exist in read mode sets errno to ENOENT.
		// ENOENT = No such file or directory
		if (errno != ENOENT) {
			perror("fopen('/tmp/chinook.pid', 'r') for starting server failed");
			return -1;
		}
	}
	FILE *fp = fopen("/tmp/chinook.pid", "w");
	if (fp == NULL) {
		perror("fopen('/tmp/chinook.pid', 'w') for starting server failed");
		return -1;
	}
	// the child process will be the server
	// the parent will write pid to chinook.pid
	const pid_t pid = fork();
	if (pid == -1) {
		perror("fork() failed");
		return -1;
	}
	if (pid == 0) {
		// placeholder code, not handling errors, replace this with the server code
		// child process
		// start_server(); // TODO: later
		// write log to ~/chinook_log.txt
		// temporary code, check it works, replace with start_server();
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/chinook_log.txt", getenv("HOME"));
		printf("%s\n", path);
		FILE *child_fp = fopen(path, "a");
		if (child_fp == NULL) {
			perror("fopen failed");
			exit(EXIT_FAILURE);
		}
		while (1) {
			fprintf(child_fp, "hello\n");
			fflush(child_fp);
			sleep(1);
		}
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
	printf("started server\n");
	return 0;
}

int stop(void) {
	FILE *fp = fopen("/tmp/chinook.pid", "r");
	if (fp == NULL) {
		// ENOENT = No such file or directory
		if (errno == ENOENT) {
			fprintf(stderr, "server not running!\n");
			return -1;
		}
		perror("fopen('tmp/chinook.pid', 'r') for stopping failed");
		return -1;
	}
	pid_t pid;
	size_t bytes_read = fread(&pid, sizeof(pid), 1, fp);
	if (bytes_read == 0) {
		if (ferror(fp)) {
			perror("fread failed");
			return -1;
		}
		fprintf(stderr, ".pid file exists but is empty, try starting server?\n");
		return -1;
	}
	if (fclose(fp) == EOF) {
		perror("fclose failed");
		return -1;
	}
	printf("pid: %i\n", pid);
	if (kill(pid, SIGKILL) == -1) {
		perror("kill failed");

	}
	if (remove("/tmp/chinook.pid") == -1) {
		perror("remove failed");
		return -1;
	}
	printf("stopped server\n");
	return 0;
}

int restart(void) {
	if (stop() == -1 || start() == -1)
		return -1;
	return 0;
}

int main(int argc, char *argv[]) {
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

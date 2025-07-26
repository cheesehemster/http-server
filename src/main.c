#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/errno.h>

#include "server.h"
#include "log.h"

enum file_exists_stat {
	FILE_EXISTS = 1,
	FILE_ERROR = -1,
	FILE_NOT_EXISTS = 0
};

enum file_exists_stat file_exists(const char *path);

int write_pid(const pid_t pid, const char *path);

int start(void);

int stop(void);

int restart(void);

int reset_log(void);

int force_stop(void);

int stop_signal(const int signal);

// TODO: fix restart()

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
	if (strcmp(argv[1], "reset-log") == 0)
		return reset_log() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	if (STR_EQ(argv[1], "force-stop")) {
		return force_stop() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	printf("chinook: unknown command: '%s'. for help, 'chinook help'\n", argv[1]);
	return EXIT_FAILURE;
}

#define PARENT_OF(pid) (pid != 0)
#define CHILD_OF(pid) (pid == 0)
#define PRINT_FAIL() (printf("an error occurred\n"))
#define DEV_MODE 1

/* using a main, monitor, and server process.
 * the server can have a fatal shutdown, but the monitor will catch it and remove the pid
 * the main process so that the user won't have to keep the terminal open
 */

int start(void) {
	// chinook.pid only exists when the server is running, check if it exists
	{
		const enum file_exists_stat file_status = file_exists("/tmp/chinook.pid");
		switch (file_status) {
			case FILE_ERROR: return -1;
			case FILE_EXISTS: return printf("server already started!\n"), 0;
			case FILE_NOT_EXISTS: break;
		}
	}
	const pid_t pid_monitor = fork();
	if (pid_monitor == -1) {
		sys_error_printf("fork failed");
		PRINT_FAIL();
		return -1;
	}
	if (PARENT_OF(pid_monitor)) { // main process
		// debugging for stdout, get rid in prod
		// TODO: get rid in prod
		if (DEV_MODE) {
			waitpid(pid_monitor, nullptr, 0);
			sleep(5);
		}
		return 0;
	}
	// monitor process
	const pid_t pid_server = fork();
	if (pid_server == -1) {
		sys_error_printf("fork failed");
		exit(EXIT_FAILURE);
	}
	if (PARENT_OF(pid_server)) {
		setpgid(0, 0); // independent process, process leader is itself.
		// monitor process
		if (write_pid(pid_server, "/tmp/chinook.pid") == -1) {
			if (kill(pid_server, SIGTERM) == -1) {
				sys_error_printf("kill failed");
				kill(pid_server, SIGKILL);
			}
			// waiting until killed by the kill call
			waitpid(pid_server, nullptr, 0);
			if (remove("/tmp/chinook.pid") == -1) {
				sys_error_printf("remove failed");
			}
			exit(EXIT_FAILURE);
		}
		// waiting until killed by user 'chinook stop'
		waitpid(pid_server, nullptr, 0);
		if (remove("/tmp/chinook.pid") == -1) {
			sys_error_printf("remove failed");
			exit(EXIT_FAILURE);
		}
		exit(EXIT_SUCCESS);
	}
	setpgid(0, 0);
	// server process
	struct server_options opt;
	opt.addr = "0.0.0.0";
	opt.port = 80;
	opt.protocol = IPV4;
	opt.special.backlog = 10000;
	lprintf(LOG, "SERVER START");
	const int status = run_server(&opt);
	lprintf(LOG, "SERVER STOP");
	exit(status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

int stop_signal(const int signal) {
	FILE *fp = fopen("/tmp/chinook.pid", "r");
	if (fp == nullptr) {
		if (errno == ENOENT) {
			printf("server not running!\n");
			return -1;
		}
		sys_error_printf("fopen failed");
		printf("an error occurred\n");
		return -1;
	}
	pid_t pid;
	const size_t bytes_read = fread(&pid, sizeof(pid), 1, fp);
	if (bytes_read == 0) {
		if (ferror(fp)) {
			sys_error_printf("fread failed");
		} else {
			lprintf(ERROR, ".pid file exists but is empty");
		}
		if (fclose(fp) == EOF) {
			sys_error_printf("fclose failed");
		}
		printf("an error occurred\n");
		return -1;
	}
	if (fclose(fp) == EOF) {
		sys_error_printf("fclose failed");
		printf("an error occurred\n");
		return -1;
	}
	lprintf(DEBUG, "here");
	if (kill(pid, signal) == -1) {
		sys_error_printf("kill failed");
		if (errno != ESRCH) {
			printf("stop server failed\n");
			return -1;
		}
		printf("stop server failed, no such process, you might of stopped the process manually, continuing...\n");
	}
	printf("stopped server successfully!\n");
	return 0;
}

int force_stop(void) {
	return stop_signal(SIGKILL) == 0 ? 0 : -1;
}

int stop(void) {
	return stop_signal(SIGTERM) == 0 ? 0 : -1;
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


enum file_exists_stat file_exists(const char *path) {
	FILE *fp = fopen(path, "r");
	if (fp != NULL) {
		if (fclose(fp) == EOF) {
			sys_error_printf("fclose failed");
			return FILE_ERROR;
		}
		return FILE_EXISTS;
	}
	if (errno != ENOENT) {
		sys_error_printf("fopen failed");
		return FILE_ERROR;
	}
	return FILE_NOT_EXISTS;
}

int write_pid(const pid_t pid, const char *path) {
	if (path == NULL) {
		lprintf(ERROR, "path is NULL");
		return -1;
	}
	FILE *fp = fopen(path, "w");
	if (fp == NULL) {
		sys_error_printf("fopen failed");
		return -1;
	}
	int retval = 0;
	const size_t bytes_written = fwrite(&pid, sizeof(pid), 1, fp);
	if (bytes_written == 0) {
		sys_error_printf("fwrite failed");
		retval = -1;
	}
	if (fclose(fp) == EOF) {
		sys_error_printf("fclose failed");
		return -1;
	}
	return retval;
}

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syslimits.h>
#include <pthread.h>
#include <string.h>
#include <sys/errno.h>

#include "log.h"

#define VERBOSE_MODE 0
#define DEBUG_MODE 1
#define LOG_STDOUT_MODE 1

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

int write_log_vprintf(const char *format, const va_list args) {
	char log_file_path[PATH_MAX];
	snprintf(log_file_path, PATH_MAX, "%s/chinook_log.txt", getenv("HOME"));
	FILE *fp = fopen(log_file_path, "a");
	if (fp == NULL) {
		perror("fopen failed");
		return -1;
	}
	vfprintf(fp, format, args);
	if (fclose(fp) == EOF) {
		perror("fclose failed");
		return -1;
	}
	return 0;
}

int log_format_vprintf(const enum LogLevel level, const char *format, const va_list args) {
	const char *log_prefix = "";
	const char *log_colour = "";
	switch (level) {
		case LOG:
			log_prefix = "[LOG]";
			break;
		case DEBUG:
			log_prefix = "[DEBUG]";
			break;
		case WARN:
			log_colour = "\033[48;2;255;100;0m\033[97m"; // orange background, white text
			log_prefix = "[WARN]";
			break;
		case ERROR:
			log_colour = "\033[48;2;230;0;0m\033[97m"; // red background, white text
			log_prefix = "[ERROR]";
			break;
		case CRITICAL_ERROR:
			log_colour = "\033[48;2;230;0;0m\033[97m"; // red background, white text
			log_prefix = "[CRITICAL_ERROR]";
			break;
		default:
			fprintf(stderr, "enum fall through case in %s()\n", __func__);
			return -1;
	}
	char log_msg_buf_ansi[1000];
	char log_msg_buf[1000];
	char iso_time[100];
	{
		const time_t t = time(NULL);
		const struct tm *ptr = gmtime(&t);
		strftime(iso_time, 100, "%Y-%m-%dT%H:%M:%SZ", ptr);
	}
	snprintf(log_msg_buf_ansi, 1000, "%s%s %-10s %s\n\033[0m", log_colour, iso_time, log_prefix, format);
	snprintf(log_msg_buf, 1000, "%s %-10s %s\n", iso_time, log_prefix, format);
	write_log_vprintf(log_msg_buf, args);
	vfprintf(stdout, log_msg_buf_ansi, args);
	return 0;
}

int ltvprintf(const enum LogLevel level, const char *file, const unsigned int line, const char *func, const char *format, const va_list args) {
	char buf[1000];
	vsnprintf(buf, 1000, format, args);
	char buf2[1000];
	snprintf(buf2, 1000, "%-100s --%s:%d %s()", buf, file, line, func);
	log_format_vprintf(level, buf2, args);
	return 0;
}

int ltprintf(const enum LogLevel level, const char *file, const unsigned int line, const char *func, const char *format, ...) {
	/*
	if (level != ERROR) {
		return 0;
	}
	*/
	va_list args;
	va_start(args, format);
	ltvprintf(level, file, line, func, format, args);
	va_end(args);
	return 0;
}

int sys_error_tprintf(const char *msg, const char *file, const unsigned int line, const char *func, ...) {
	char format[1000];
	snprintf(format, 1000, "\"%s\": %s", msg, strerror(errno));
	va_list args;
	va_start(args, func);
	ltvprintf(ERROR, file, line, func, format, args);
	va_end(args);
	return 0;
}

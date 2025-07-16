#ifndef LOG_H
#define LOG_H

enum LogLevel {
	LOG,
	DEBUG,
	WARN,
	ERROR,
	CRITICAL_ERROR
};

/*int log_printf(const char*, ...); // writes to the log file and writes to stdout when log stdout mode is on
int debug_printf(const char*, ...); // writes to stdout when debug mode is on
int verbose_printf(const char*, ...); // writes to stdout when verbose mode is on
int warn_printf(const char *, ...);
int error_printf(const char *, ...);*/
int sys_error_printf(const char *, const char *, ...);
int lprintf(const enum LogLevel level, const char *format, ...);

#endif //LOG_H

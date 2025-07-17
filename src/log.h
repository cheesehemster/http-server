#ifndef LOG_H
#define LOG_H

#define lprintf(level, format, ...) \
    ltprintf(level, __FILE_NAME__, __LINE__, __FUNCTION__, format __VA_OPT__(,) __VA_ARGS__)

#define sys_error_printf(msg, ...) \
	sys_error_tprintf(msg, __FILE_NAME__, __LINE__, __FUNCTION__ __VA_OPT__(,) __VA_ARGS__)

enum LogLevel {
	LOG,
	DEBUG,
	WARN,
	ERROR,
	CRITICAL_ERROR
};

int ltprintf(const enum LogLevel level, const char *file, const unsigned int line, const char *func, const char *format, ...);
int sys_error_tprintf(const char *msg, const char *file, const unsigned int line, const char *func, ...);

#endif //LOG_H

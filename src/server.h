#ifndef MAIN_H
#define MAIN_H

#define REQUEST_MAX_SIZE_BYTES (1000000 * 1) // 1mb
#define STR_EQ(a, b) (strcmp(a, b) == 0)
#define LOGGING_ENABLED 1

enum ip_protocol {
	IPV4, IPV6
};

struct server_options {
	enum ip_protocol protocol;
	char *addr;
	unsigned short port;

	struct {
		int backlog;
	} special;
};

int run_server(const struct server_options *opt);

void print_str_no_cr(const char *str);

#define CONCAT_(a, b) a##b
#define CONCAT(a, b) CONCAT_(a, b)
#define PP_NARG(...) \
PP_NARG_(__VA_ARGS__,PP_RSEQ_N())
#define PP_NARG_(...) \
PP_ARG_N(__VA_ARGS__)
#define PP_ARG_N( \
	_1, _2, _3, _4, _5,  N, ...) N
#define PP_RSEQ_N() \
5, 4, 3, 2, 1, 0
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define MAX3(a, b, c) (MAX2(a, b) > (c) ? MAX2(a, b) : (c))
#define MAX4(a, b, c, d) (MAX3(a, b, c) > (d) ? MAX3(a, b, c) : (d))
#define MAX5(a, b, c, d, e) (MAX4(a, b, c, d) > (e) ? MAX4(a, b, c, d) : (e))
#define MAX(...) CONCAT(MAX, PP_NARG(__VA_ARGS__))(__VA_ARGS__)

#endif

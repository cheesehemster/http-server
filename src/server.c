#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// ReSharper disable once CppUnusedIncludeDirective
#include <stdatomic.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/errno.h>

#include "server.h"
#include "parse_http.h"
#include "log.h"
#include "thread_pool.h"

// TODO: cache
// TODO: compression
// TODO: https

#define GOTO_ERR (-1)
#define CONTINUE (-2)
#define TIMEOUT (-3)
#define CLOSED (-4)

static int signal_pipe_fds[2];
static int thread_error_pipe_fds[2];
static atomic_bool thread_error;
static volatile sig_atomic_t sig;
static atomic_bool shutdown_requested;
static atomic_uint open_connections;

void signal_handler(const int signum);

int setup_sig_handler();

int cleanup(const int fd);

void setup_atomic(void);

int setup_pipe(void);

int ip_socket(enum ip_protocol ipp);

socklen_t ip_sockaddr(struct sockaddr_storage *sockaddr, const struct server_options *opt);

int listen_socket(const struct server_options *opt);

enum wait_request_status {
	WAIT_SUCCESS = 0,
	WAIT_SIGNAL_INTERRUPTED = -1,
	WAIT_THREAD_ERROR = -2,
	WAIT_FAILED = -3
};

enum wait_request_status wait_request(const int listen_fd, const int signal[2], const int thread_err[2]);

int connect_client(const int listen_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);

char *sockaddr_get_ip_str(const struct sockaddr_storage *socka, char buf[], const socklen_t bufn);

ssize_t get_response(const int conn_fd, char buf[], const size_t bufn);

void print_str_no_cr(const char *str);

void *handle_connection(void *vargp);

struct __attribute__((__packed__)) thread_args {
	int listen_fd;
	int client_fd;
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len;
};

static int setup(void);

// TODO: fix accept error with O_NONBLOCK

int run_server(const struct server_options *opt) {
	if (setup() == -1)
		return -1;
	const struct thread_pool_attr attr = {.pool_size = 1000, .queue_size = 10000};
	thread_pool_t *tp = thread_pool_create(&attr);
	if (tp == NULL) {
		return -1;
	}
	const int listen_fd = listen_socket(opt);
	if (listen_fd == -1)
		return -1;
	while (1) {
		const enum wait_request_status stat = wait_request(listen_fd, signal_pipe_fds, thread_error_pipe_fds);
		switch (stat) {
			case WAIT_SUCCESS: break;
			case WAIT_SIGNAL_INTERRUPTED: goto signal_interrupt_cleanup;
			case WAIT_THREAD_ERROR: goto error_cleanup;
			case WAIT_FAILED: goto error_cleanup;
		}
		struct sockaddr_storage client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		const int client_fd = connect_client(listen_fd, &client_addr, &client_addr_len);
		if (client_fd == -1)
			goto error_cleanup;
		struct thread_args *targs = malloc(sizeof(*targs));
		if (targs == NULL) {
			sys_error_printf("malloc failed");
			goto error_cleanup;
		}
		targs->listen_fd = listen_fd;
		targs->client_addr = client_addr;
		targs->client_addr_len = client_addr_len;
		targs->client_fd = client_fd;

		thread_pool_add_task(tp, handle_connection, targs);
		/*
		pthread_t thread_id;
		pthread_create(&thread_id, nullptr, handle_connection, targs);
		pthread_detach(thread_id);
		*/
	}
error_cleanup:
	lprintf(LOG, "an error occurred, cleaning up...");
	thread_pool_shutdown_graceful(tp);
	thread_pool_destroy(tp);
	atomic_store(&shutdown_requested, true);
	while (atomic_load(&open_connections) != 0) {
		usleep(10'000); // 10ms
	}
	if (cleanup(listen_fd) == -1)
		return -1;
	return -1;
signal_interrupt_cleanup:
	lprintf(LOG, "a signal interrupted, cleaning up...");
	thread_pool_shutdown_graceful(tp);
	thread_pool_destroy(tp);
	atomic_store(&shutdown_requested, true);
	while (atomic_load(&open_connections) != 0) {
		usleep(10'000); // 10ms
	}
	if (cleanup(listen_fd) == -1)
		return -1;
	return 0;
}

static int setup(void) {
	setup_atomic();
	if (setup_pipe() == -1 || setup_sig_handler() == -1)
		return -1;
	return 0;
}

void *handle_connection(void *vargp) {
	atomic_fetch_add(&open_connections, 1);
	struct thread_args *args = vargp;
	const int client_fd = args->client_fd;
	const struct sockaddr_storage client_addr = args->client_addr;
	const socklen_t client_addr_len = args->client_addr_len;
	const int listen_fd = args->listen_fd;
	lprintf(DEBUG, "TCP CONNECTED"); {
		char ip_str_buf[1000];
		lprintf(DEBUG, "client ip: %s", sockaddr_get_ip_str(&client_addr, ip_str_buf, sizeof(ip_str_buf)));
	}
	int keep_alive = 1;
	while (keep_alive) {
		char *buf = malloc(REQUEST_MAX_SIZE_BYTES);
		const ssize_t len = get_response(client_fd, buf, REQUEST_MAX_SIZE_BYTES);
		if (len < 0) {
			free(buf);
			switch (len) {
				case GOTO_ERR: goto error_cleanup;
				case CONTINUE:
				case CLOSED:
				case TIMEOUT: goto next;
				default: lprintf(ERROR, "return code fall through case at %s");
			}
		}
		struct HttpRequest req;
		if (parse_http_request(buf, &req) == -1) {
			free(buf);
			goto error_cleanup;
		}
		if (get_http_header("Connection", &req.headers) && (
			    strcmp(get_http_header("Connection", &req.headers), "close") == 0)) {
			lprintf(DEBUG, "client sent Connection: close");
			keep_alive = 0;
		}
		lprintf(LOG, "%s", req.request_line.uri);
		char *data;
		if (keep_alive) {
			data = "HTTP/1.1 200 Ok\r\nContent-Type: application/json\r\nContent-Length:6\r\nConnection: keep-alive\r\n\r\nhello!";
		} else {
			data = "HTTP/1.1 200 Ok\r\nContent-Type: application/json\r\nContent-Length:6\r\nConnection: close\r\n\r\nhello!";
		}
		free(buf);
		if (send(client_fd, data, strlen(data), 0) == -1) {
			sys_error_printf("send failed");
			goto error_cleanup;
		}
	}
	// TODO: buffer for logging,
	// TODO: thread for printing the buffer
next:
	shutdown(client_fd, SHUT_WR);
	free(args);
	usleep(100);
	close(client_fd);
	lprintf(DEBUG, "TCP DISCONNECTED");
	atomic_fetch_sub(&open_connections, 1);
	return NULL;
error_cleanup:
	shutdown(client_fd, SHUT_WR);
	free(args);
	close(client_fd);
	lprintf(DEBUG, "TCP DISCONNECTED");
	if (atomic_load(&thread_error) == true) {
		return NULL;
	}
	atomic_store(&thread_error, true);
	char data = 'a';
	write(thread_error_pipe_fds[1], &data, 1);
	atomic_fetch_sub(&open_connections, 1);
	return NULL;
}


void signal_handler(const int signum) {
	constexpr char buf = 'a';
	if (write(signal_pipe_fds[1], &buf, sizeof(buf)) == -1) {
		char err_msg[256];
		strerror_r(errno, err_msg, sizeof(err_msg));
		write(STDERR_FILENO, err_msg, sizeof(err_msg));
		exit(EXIT_FAILURE);
	}
	sig = signum;
}

int setup_sig_handler() {
	struct sigaction sa;
	sa.__sigaction_u.__sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; // if a function gets interrupted by signal, restart the function
	if (sigaction(SIGINT, &sa, nullptr) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
		sys_error_printf("sigaction failed");
		return -1;
	}
	return 0;
}

void setup_atomic(void) {
	atomic_init(&thread_error, false);
	atomic_init(&open_connections, 0);
	atomic_init(&shutdown_requested, false);
}

int setup_pipe(void) {
	if (pipe(signal_pipe_fds) == -1 || pipe(thread_error_pipe_fds) == -1) {
		sys_error_printf("pipe failed");
		return -1;
	}
	return 0;
}

int cleanup(const int fd) {
	if (close(fd) == -1) {
		sys_error_printf("close failed");
		return -1;
	}
	return 0;
}


int ip_socket(enum ip_protocol ipp) {
	int fd;
	switch (ipp) {
		case IPV4:
			fd = socket(AF_INET, SOCK_STREAM, 0);
			break;
		case IPV6:
			fd = socket(AF_INET6, SOCK_STREAM, 0);
			break;
		default:
			lprintf(ERROR, "enum fallthrough ip_socket");
			return -1;
	}
	if (fd == -1) {
		sys_error_printf("socket failed");
		return -1;
	}
	return fd;
}


// TODO: error handling
socklen_t ip_sockaddr(struct sockaddr_storage *sockaddr, const struct server_options *opt) {
	switch (opt->protocol) {
		case IPV4: {
			struct sockaddr_in *tmp = (struct sockaddr_in *) sockaddr;
			tmp->sin_len = 0;
			tmp->sin_family = AF_INET;
			tmp->sin_port = htons(opt->port);
			if (opt->addr == NULL) {
				tmp->sin_addr.s_addr = htonl(INADDR_ANY);
			} else {
				uint32_t buf;
				inet_pton(AF_INET, opt->addr, &buf);
				tmp->sin_addr.s_addr = buf;
			}
			memset(tmp->sin_zero, 0, sizeof(tmp->sin_zero));
			return sizeof(struct sockaddr_in);
		}
		case IPV6: {
			struct sockaddr_in6 *tmp = (struct sockaddr_in6 *) sockaddr;
			tmp->sin6_len = 0;
			tmp->sin6_family = AF_INET6;
			tmp->sin6_port = htons(opt->port);
			if (opt->addr == NULL) {
				tmp->sin6_addr = in6addr_any;
			} else {
				uint32_t buf[4];
				inet_pton(AF_INET6, opt->addr, &buf);
				tmp->sin6_addr.__u6_addr.__u6_addr32[0] = buf[0];
				tmp->sin6_addr.__u6_addr.__u6_addr32[1] = buf[1];
				tmp->sin6_addr.__u6_addr.__u6_addr32[2] = buf[2];
				tmp->sin6_addr.__u6_addr.__u6_addr32[3] = buf[3];
			}
			tmp->sin6_scope_id = 0;
			tmp->sin6_flowinfo = 0;
			return sizeof(struct sockaddr_in6);
		}
		default:
			lprintf(ERROR, "enum fallthrough");
			return 0;
	}
}

// TODO: thread pool
// TODO: memory pool
// TODO: thread for logs
// TODO: accept O_NONBLOCK bug

int listen_socket(const struct server_options *opt) {
	const int socket_fd = ip_socket(opt->protocol);
	if (socket_fd == -1) {
		return -1;
	}
	constexpr int val = 1;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == -1) {
		sys_error_printf("setsockopt failed");
		goto cleanup_fail;
	}
	/*
	if (fcntl(socket_fd, F_SETFD, O_NONBLOCK) == -1) {
		sys_error_printf("fcntl failed");
		return -1;
	}*/
	struct sockaddr_storage bind_addr;
	const socklen_t size = ip_sockaddr(&bind_addr, opt);
	if (size == 0)
		goto cleanup_fail;
	if (bind(socket_fd, (struct sockaddr *) &bind_addr, size) == -1) {
		sys_error_printf("bind failed");
		goto cleanup_fail;
	}
	if (listen(socket_fd, opt->special.backlog) == -1) {
		sys_error_printf("listen failed");
		goto cleanup_fail;
	}
	return socket_fd;
cleanup_fail:
	close(socket_fd);
	return -1;
}

enum wait_request_status wait_request(const int listen_fd, const int signal[2], const int thread_err[2]) {
	fd_set fd_set;
	FD_ZERO(&fd_set);
	FD_SET(listen_fd, &fd_set);
	FD_SET(signal[0], &fd_set);
	FD_SET(thread_err[0], &fd_set);
	if (select(MAX(listen_fd, signal[0], thread_err[0]) + 1, &fd_set, NULL, NULL, NULL) == -1) {
		if (errno == EINTR) {
			lprintf(DEBUG, "signal interrupted during select");
			return WAIT_SIGNAL_INTERRUPTED;
		}
		sys_error_printf("select failed");
		return WAIT_FAILED;
	}
	if (FD_ISSET(signal[0], &fd_set)) {
		return WAIT_SIGNAL_INTERRUPTED;
	}
	if (FD_ISSET(thread_err[0], &fd_set)) {
		return WAIT_THREAD_ERROR;
	}
	// listen fd ready
	return WAIT_SUCCESS;
}

int connect_client(const int listen_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len) {
	int client_fd = accept(listen_fd, (struct sockaddr *) client_addr, client_addr_len);
	if (client_fd == -1) {
		sys_error_printf("accept failed");
		return -1;
	}
	struct timeval t;
	t.tv_sec = 10;
	t.tv_usec = 0;
	if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == -1) {
		sys_error_printf("setsockopt failed");
	}
	return client_fd;
}

char *sockaddr_get_ip_str(const struct sockaddr_storage *socka, char buf[], const socklen_t bufn) {
	void *addr;
	switch (socka->ss_family) {
		case AF_INET:
			//lprintf(DEBUG, "ipv4 client");
			addr = &((struct sockaddr_in *) socka)->sin_addr;
			break;
		case AF_INET6:
			//lprintf(DEBUG, "ipv6 client");
			addr = &(((struct sockaddr_in6 *) socka)->sin6_addr);
			break;
		default:
			lprintf(ERROR, "client unsupported protocol");
			return NULL;
	}
	inet_ntop(socka->ss_family, addr, buf, bufn);
	return buf;
}

ssize_t get_response(const int conn_fd, char buf[], const size_t bufn) {
	const ssize_t msglen = recv(conn_fd, buf, bufn, 0);
	if (msglen == -1) {
		if (errno == EAGAIN) {
			// connection timeout
			lprintf(LOG, "connection timeout");
			return TIMEOUT;
		}
		sys_error_printf("recv failed");
		return GOTO_ERR;
	}
	if (msglen == 0) {
		lprintf(DEBUG, "client closed tcp");
		return CLOSED;
	}
	if ((size_t) msglen >= bufn) {
		lprintf(ERROR, "request size larger than max size, (%zu bytes)", bufn);
		return CONTINUE;
	}
	return msglen;
}

void print_str_no_cr(const char *str) {
	for (const char *ptr = str; *ptr; ptr++) {
		if (*ptr == 13)
			continue;
		printf("%c", *ptr);
	}
}

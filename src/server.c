#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
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

// TODO: cache
// TODO: compression
// TODO: https

int pipe_fds[2];
atomic_bool thread_error;
volatile sig_atomic_t sig;

void signal_handler(int signum) {
	char buf = 'a';
	if (write(pipe_fds[1], &buf, sizeof(buf)) == -1) {
		sys_error_printf("write failed");
		lprintf(ERROR, "writing to pipe failed, exiting without cleanup");
		exit(EXIT_FAILURE);
	}
	sig = signum;
}

int setup_sig_handler() {
	struct sigaction sa;
	sa.__sigaction_u.__sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; // if a function gets interrupted by signal, restart the function
	if (sigaction(SIGINT, &sa, nullptr) == -1 || sigaction(SIGTERM, &sa, NULL)) {
		sys_error_printf("sigaction failed");
		return -1;
	}
	return 0;
}

int cleanup(int fd) {
	if (close(fd) == -1) {
		sys_error_printf("close failed");
		return -1;
	}
	return 0;
}

void setup_atomic() {
	atomic_init(&thread_error, true);
}

int setup_pipe() {
	if (pipe(pipe_fds) == -1) {
		sys_error_printf("pipe failed");
		return -1;
	}
	return 0;
}

enum IpProtocol {
	IPV4, IPV6
};

int ip_socket(enum IpProtocol ipp) {
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


// 0 on failure
socklen_t ip_sockaddr(struct sockaddr_storage* sockaddr, const enum IpProtocol ipp, const char* addr,
                      const unsigned short port) {
	switch (ipp) {
		case IPV4: {
			struct sockaddr_in* tmp = (struct sockaddr_in *) sockaddr;
			tmp->sin_len = 0;
			tmp->sin_family = AF_INET;
			tmp->sin_port = htons(port);
			if (addr == NULL) {
				tmp->sin_addr.s_addr = htonl(INADDR_ANY);
			} else {
				uint32_t buf;
				inet_pton(AF_INET, addr, &buf);
				tmp->sin_addr.s_addr = buf;
			}
			memset(tmp->sin_zero, 0, sizeof(tmp->sin_zero) / sizeof(tmp->sin_zero[0]));
			return sizeof(struct sockaddr_in);
		}
		case IPV6: {
			struct sockaddr_in6* tmp = (struct sockaddr_in6 *) sockaddr;
			tmp->sin6_len = 0;
			// TODO: sin6_addr
			if (addr == NULL) {
				tmp->sin6_addr = in6addr_any;
			} else {
				uint32_t buf[4];
				inet_pton(AF_INET6, addr, &buf);
				tmp->sin6_addr.__u6_addr.__u6_addr32[0] = buf[0];
				tmp->sin6_addr.__u6_addr.__u6_addr32[1] = buf[1];
				tmp->sin6_addr.__u6_addr.__u6_addr32[2] = buf[2];
				tmp->sin6_addr.__u6_addr.__u6_addr32[3] = buf[3];
			}
			tmp->sin6_family = AF_INET6;
			tmp->sin6_port = htons(port);
			tmp->sin6_scope_id = 0;
			tmp->sin6_flowinfo = 0;
			return sizeof(struct sockaddr_in6);
		}
		default:
			lprintf(ERROR, "enum fallthrough ip_sockaddr");
			return 0;
	}
}


// TODO: string for ip addr
int setup_server_socket(const enum IpProtocol protocol, const char* ip_addr, const unsigned short port,
                        const int backlog) {
	const int socket_fd = ip_socket(IPV4);
	if (socket_fd == -1)
		return -1;
	const int val = 1;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == -1) {
		sys_error_printf("setsockopt failed");
		goto cleanup_fail;
	}
	struct timeval t;
	t.tv_sec = 0;
	t.tv_usec = 10; // dont let the listen socket block
	if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == -1) {
		sys_error_printf("setsockopt failed");
		goto cleanup_fail;
	}
	struct sockaddr_storage addr;
	const socklen_t size = ip_sockaddr(&addr, protocol, ip_addr, port);
	if (size == 0)
		goto cleanup_fail;
	if (bind(socket_fd, (struct sockaddr *) &addr, size) == -1) {
		sys_error_printf("bind failed");
		goto cleanup_fail;
	}
	if (listen(socket_fd, backlog) == -1) {
		sys_error_printf("listen failed");
		goto cleanup_fail;
	}
	return socket_fd;
cleanup_fail:
	close(socket_fd);
	return -1;
}

enum wait_request_status {
	WAIT_FIN = 0,
	WAIT_INTERRUPTED = -1
};

enum wait_request_status wait_request(const int listen_fd, int pipefds[2]) {
	fd_set fd_set;
	FD_ZERO(&fd_set);
	FD_SET(listen_fd, &fd_set);
	FD_SET(pipefds[0], &fd_set);
	select(MAX(listen_fd, pipefds[0]) + 1, &fd_set, NULL, NULL, NULL); // blocks indefinitely until ready
	if (FD_ISSET(pipefds[0], &fd_set)) {
		return WAIT_INTERRUPTED;
	}
	return WAIT_FIN;
}

int connect_client(const int listen_fd, struct sockaddr_storage* client_addr, socklen_t* client_addr_len) {
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

char* sockaddr_get_ip_str(const struct sockaddr_storage* socka, char buf[], const socklen_t bufn) {
	void* addr;
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

#define GOTO_ERR (-1)
#define CONTINUE (-2)
#define TIMEOUT (-3)
#define CLOSED (-4)

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

void print_str_no_cr(const char* str) {
	for (const char* ptr = str; *ptr; ptr++) {
		if (*ptr == 13)
			continue;
		printf("%c", *ptr);
	}
}

struct server_options {
	enum IpProtocol protocol;
	char* addr;
	short port;

	struct {
		int backlog;
	} special;
};

struct thread_args {
	int listen_fd;
};

void* handle_connection(void* vargp) {
	struct thread_args* args = (struct thread_args *) vargp;
	int listen_fd = args->listen_fd;
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	const int client_fd = connect_client(listen_fd, &client_addr, &client_addr_len);
	if (client_fd == -1)
		goto error_cleanup;
	lprintf(DEBUG, "TCP CONNECTED");
	{
		char ip_str_buf[1000];
		lprintf(DEBUG, "client ip: %s", sockaddr_get_ip_str(&client_addr, ip_str_buf, sizeof(ip_str_buf)));
	}
	int keep_alive = 1;
	while (keep_alive) {
		char buf[REQUEST_MAX_SIZE_BYTES];
		const ssize_t len = get_response(client_fd, buf, REQUEST_MAX_SIZE_BYTES);
		if (len < 0) {
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
			goto error_cleanup;
		}
		if (get_http_header("Connection", &req.headers) && !STRING_EQ(get_http_header("Connection", &req.headers),
		                                                              "keep-alive")) {
			lprintf(DEBUG, "client sent Connection: close");
			keep_alive = 0;
		}
		lprintf(LOG, "%s", req.request_line.uri);
		const char* data =
			"HTTP/1.1 200 Ok\r\nContent-Type: application/json\r\nContent-Length:6\r\nConnection: keep-alive\r\n\r\nhello!";
		if (send(client_fd, data, strlen(data), 0) == -1) {
			sys_error_printf("send failed");
			goto error_cleanup;;
		}
	}
next:
	close(client_fd);
	lprintf(DEBUG, "TCP DISCONNECTED");
	return NULL;
}

int run_server() {
	setup_atomic();
	if (setup_pipe() == -1 || setup_sig_handler() == -1)
		return -1;
	int listen_fd = setup_server_socket(IPV4, "0.0.0.0", 80, 100);
	if (listen_fd == -1)
		return -1;
	while (1) {
		if (wait_request(listen_fd, pipe_fds) == WAIT_INTERRUPTED)
			goto signal_interrupt_cleanup;
		pthread_t thread_id;
		struct thread_args targs;
		targs.listen_fd = listen_fd;
		pthread_create(&thread_id, nullptr, handle_connection, &targs);
		pthread_detach(thread_id);
	}
error_cleanup:
	lprintf(LOG, "an error occurred, cleaning up...");
	if (cleanup(listen_fd) == -1)
		return EXIT_FAILURE;
	return EXIT_FAILURE;
signal_interrupt_cleanup:
	lprintf(LOG, "a signal interrupted, cleaning up...");
	if (cleanup(listen_fd) == -1)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

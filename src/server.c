#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <arpa/inet.h>

#include "server.h"
#include "parse_http.h"
#include "log.h"

#define BACKLOG 100

// TODO: http parser
// TODO: http header Connection:
// TODO: cache
// TODO: compression
// TODO: https

int fildes[2];
// sig in case it's necessary
volatile sig_atomic_t sig;

void handler(int signum) {
	char buf = 'e';
	if (write(fildes[1], &buf, 1) == -1) {
		sys_error_printf("write failed", __func__);
		lprintf(ERROR, "writing to pipe failed, exiting without cleanup");
		// since the program cannot exit from pipe, exit early before anything bad happens.
		// maybe refactor later to retry writing
		exit(EXIT_FAILURE);
	}
	sig = signum;
}

int setup_sig_handler() {
	struct sigaction sa;
	sa.__sigaction_u.__sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; // if a function gets interrupted by signal, restart the function
	if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL)) {
		sys_error_printf("sigaction failed", __func__);
		return -1;
	}
	return 0;
}

int cleanup(int fd) {
	if (close(fd) == -1) {
		sys_error_printf("close failed", __func__);
		return -1;
	}
	return 0;
}

int setup_pipe() {
	if (pipe(fildes) == -1) {
		sys_error_printf("pipe failed", __func__);
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
		sys_error_printf("socket failed", __func__);
		return -1;
	}
	return fd;
}


// 0 on failure
socklen_t ip_sockaddr(struct sockaddr_storage* sockaddr, const enum IpProtocol ipp, const char *addr, const unsigned short port) {
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
int setup_server_socket(const enum IpProtocol protocol, const char *ip_addr, const unsigned short port, const int backlog) {
	const int socket_fd = ip_socket(IPV4);
	if (socket_fd == -1)
		return -1;
	const int val = 1;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == -1) {
		sys_error_printf("setsockopt failed", __func__);
		goto cleanup_fail;
	}
	struct timeval t;
	t.tv_sec = 0;
	t.tv_usec = 10; // dont let the listen socket block
	if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == -1) {
		sys_error_printf("setsockopt failed", __func__);
		goto cleanup_fail;
	}
	struct sockaddr_storage addr;
	const socklen_t size = ip_sockaddr(&addr, protocol, ip_addr, port);
	if (size == 0)
		goto cleanup_fail;
	if (bind(socket_fd, (struct sockaddr *) &addr, size) == -1) {
		sys_error_printf("bind failed", __func__);
		goto cleanup_fail;
	}
	if (listen(socket_fd, backlog) == -1) {
		sys_error_printf("listen failed", __func__);
		goto cleanup_fail;
	}
	return socket_fd;
cleanup_fail:
	close(socket_fd);
	return -1;
}

#define INTERRUPTED_SIGNAL (-1)

int wait_request(const int fd, const int pipefds[2]) {
	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);
	FD_SET(pipefds[0], &fdset);
	select(MAX(fd, fildes[0]) + 1, &fdset, NULL, NULL, NULL); // blocks indefinitely until ready
	if (FD_ISSET(pipefds[0], &fdset)) {
		return INTERRUPTED_SIGNAL;
	}
	return 0;
}


#define GOTO_ERR (-1)
#define CONTINUE (-2)

int connect_request(const int listen_fd) {
	struct sockaddr conn_addr;
	socklen_t conn_addr_len;
	int conn_fd = accept(listen_fd, &conn_addr, &conn_addr_len); // non-blocking, select already blocked
	if (conn_fd == -1) {
		sys_error_printf("accept failed", __func__);
		return -1;
	}
	struct timeval t;
	t.tv_sec = 0;
	t.tv_usec = 100000; // 100ms
	if (setsockopt(conn_fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == -1) {
		sys_error_printf("setsockopt failed", __func__);
	}
	return conn_fd;
}

int get_response(const int conn_fd, char buf[], const size_t bufn) {
	ssize_t msglen = recv(conn_fd, buf, bufn, 0); // THIS GRRRR // the error was that i sent the &buf which is a char **
	if (msglen == -1) {
		sys_error_printf("recv failed", __func__);
		return GOTO_ERR;
	}
	if (msglen == 0) {
		// client closed tcp connection, not an error, continue
		return CONTINUE;
	}
	if ((size_t) msglen >= bufn) {
		lprintf(ERROR, "request size larger than max size, (%zu bytes)", bufn);
		return CONTINUE;
	}
	buf[msglen] = '\0'; // GRRR YOU WASTED ALL THIS TIME // not actually the culprit, but it creates the error
	return 0;
}

void print_str_no_cr(const char* str) {
	for (const char* ptr = str; *ptr; ptr++) {
		if (*ptr == 13)
			continue;
		printf("%c", *ptr);
	}
}

int run_server(void) {
	if (setup_pipe() == -1 || setup_sig_handler() == -1)
		return -1;
	int listen_fd = setup_server_socket(IPV4, "0.0.0.0", 80, BACKLOG);
	if (listen_fd == -1)
		return -1;
	while (1) {
		if (wait_request(listen_fd, fildes) == INTERRUPTED_SIGNAL)
			goto signal_interrupt_cleanup;
		const int conn_fd = connect_request(listen_fd);
		if (conn_fd == -1)
			goto error_cleanup;
		char buf[REQUEST_MAX_SIZE_BYTES];
		int status = get_response(conn_fd, buf, REQUEST_MAX_SIZE_BYTES);
		if (status != 0) {
			close(conn_fd);
			if (status == GOTO_ERR)
				goto error_cleanup;
			if (status == CONTINUE)
				continue;
		}
		struct HttpRequest req;
		if (parse_http_request(buf, &req) == -1) {
			goto error_cleanup;
		}
		struct HttpResponse res;
		res.status_line.version = HTTP_VERSION_1_1;
		res.status_line.status_code = 200;
		char rp[] = "Ok";
		res.status_line.reason_phrase = rp;
		res.body.ptr = NULL;
		res.body.len = 0;
		print_http_request_struct(&req);
		set_http_field("Content-Type", "application/json", &res.headers);
		set_http_field("Content-Length", "6", &res.headers);
		//print_http_response_struct(&res);
		const char* data = "HTTP/1.1 200 Ok\nContent-Type: application/json\nContent-Length:6\n\nhello!";
		if (send(conn_fd, data, strlen(data), 0) == -1) {
			sys_error_printf("send failed", __func__);
			goto error_cleanup;;
		}
		close(conn_fd);
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

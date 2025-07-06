#include <stdio.h>
#include <string.h>
#include <_stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/errno.h>

#define REQUEST_MAX_SIZE_BYTES (1000000 * 1) // 1mb
#define BACKLOG 100
#define PORT 80

#define MAX(a, b) ((a) > (b) ? (a) : (b))

// TODO: http parser, http header Connection:
// TODO: cache
// TODO: compression
// TODO: https

int fildes[2];
// sig in case it's necessary
volatile sig_atomic_t sig = 0;

void handler(int signum) {
	char buf = 'e';
	if (write(fildes[1], &buf, 1) == -1) {
		perror("write failed");
		printf("writing to pipe failed, exiting without cleanup");
		// since the program cannot exit from pipe, exit early before anything bad happens.
		// maybe refactor later to retry writing
		exit(EXIT_FAILURE);
	}
	sig = signum;
}

int setupsig() {
	struct sigaction sa;
	sa.__sigaction_u.__sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; // if a function gets interrupted by signal, restart the function
	if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL)) {
		perror("sigaction failed");
		return -1;
	}
	return 0;
}

int setuppipe() {
	if (pipe(fildes) == -1) {
		perror("pipe failed");
		return -1;
	}
	return 0;
}

enum IpProtocol {
	IPV4, IPV6
};

int main(void) {
	if (setuppipe() == -1)
		return EXIT_FAILURE;
	if (setupsig() == -1)
		return EXIT_FAILURE;
	const int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	const struct sockaddr_in address = {
		// bind the listening socket to this address
		.sin_len = 0,
		.sin_family = AF_INET,
		.sin_port = htons(PORT),
		.sin_addr = {htonl(INADDR_ANY)}, // accept from any address in this system
		.sin_zero = {0}
	};
	if (bind(listen_fd, (struct sockaddr *) &address, sizeof(struct sockaddr_in)) == -1) {
		perror("bind failed");
		goto fatal;
	}
	if (listen(listen_fd, BACKLOG) == -1) {
		perror("listen failed");
		goto fatal;
	}
	while (1) {
		printf("waiting...\n");
		fd_set fdset;
		FD_ZERO(&fdset);
		FD_SET(listen_fd, &fdset);
		FD_SET(fildes[0], &fdset);
		select(MAX(listen_fd, fildes[0]) + 1, &fdset, NULL, NULL, NULL); // blocks indefinitely until ready
		if (FD_ISSET(fildes[0], &fdset)) { // pipe ready for reading
			goto sigc;
		}
		struct sockaddr conn_addr;
		socklen_t conn_addr_len;
		const int conn_fd = accept(listen_fd, &conn_addr, &conn_addr_len); // non-blocking, select already blocked
		if (conn_fd == -1) {
			perror("accept failed");
			goto fatal;
		}
		printf("connected\n");
		char buf[REQUEST_MAX_SIZE_BYTES];
		ssize_t msglen = recv(conn_fd, &buf, REQUEST_MAX_SIZE_BYTES, 0);
		if (msglen == -1) {
			perror("recv failed");
			goto fatal;
		}
		if (msglen == 0) { // client closed tcp connection
			close(conn_fd);
			continue;
		}
		if (msglen == REQUEST_MAX_SIZE_BYTES) {
			fprintf(stderr, "request size larger than max size, (%i bytes)\n", REQUEST_MAX_SIZE_BYTES);
			close(conn_fd);
			continue;
		}
		buf[msglen] = '\0';
		char *ptr = &buf[0];
		while (*ptr) {
			if (*ptr == 13) {
				ptr++;
				continue;
			}
			printf("%c", *ptr);
			ptr++;
		}
 		const char *data = "HTTP/1.1 200 Ok\nContent-Type: application/json\nContent-Length:6\n\nhello!";
		if (send(conn_fd, data, strlen(data), 0) == -1) {
			perror("send failed");
			close(conn_fd);
			goto fatal;
		}
		close(conn_fd);
	}

fatal:
	printf("an error occurred, cleaning up...\n");
	if (close(listen_fd) == -1) {
		perror(NULL);
		printf("cleaned up with error, exiting...\n");
		return EXIT_FAILURE;
	}
	printf("cleaned up successfully, exiting...\n");
	return EXIT_FAILURE;
sigc:
	printf("a signal interrupted, cleaning up...\n");
	if (close(listen_fd) == -1) {
		perror(NULL);
		printf("cleaned up with error, exiting...\n");
		return EXIT_FAILURE;
	}
	printf("cleaned up successfully, exiting...\n");
	return EXIT_SUCCESS;
}

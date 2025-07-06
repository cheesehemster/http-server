#include <stdio.h>
#include <string.h>

#define REQUEST_LINE_URI_LIMIT 1024
#define REQUEST_HEADER_FIELDS_LIMIT 100
#define REQUEST_HEADER_FIELD_SIZE 1024

// remember, something like, int x; x is a garbage value, usually 0 but cannot rely on it

enum HttpMethod {
	OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE, CONNECT
};

enum HttpVersion {
	HTTP_1_0, HTTP_1_1, HTTP_2, HTTP_3
};

struct HttpRequest {
	enum HttpMethod method;
	char uri[REQUEST_LINE_URI_LIMIT];
	enum HttpVersion version;
	char fields[REQUEST_HEADER_FIELDS_LIMIT][REQUEST_HEADER_FIELD_SIZE][2];
	size_t nfields;
	char *body;
	size_t nbody;
};

struct HttpResponse {

};

/**
 * @brief Tokenizes a string into an array buffer using a string delimiter.
 *
 * Splits the input string `str` by occurrences of the full string `sep`.
 * Each resulting token is placed in the `buf` array.
 *
 * @param[in,out] str  The input string to be split. It will be modified in-place.
 * @param[in]     sep  The string delimiter used to split `str`.
 * @param[out]    buf  Array of pointers that will be filled with token substrings.
 * @param[in]     bufn The number of elements allocated in `buf`.
 *
 * @return Number of tokens found and stored in `buf`, or -1 on error (e.g., too many tokens).
 *
 * @warning The input string `str` must remain in scope and unmodified after this function
 *          returns, since `buf` will contain pointers into `str`.
 */
int strstrtok(char *str, const char *sep, char *buf[], const size_t bufn) {
	if (bufn <= 0) {
		fprintf(stderr, "buffer size 0\n");
		return -1;
	}
	const size_t seplen = strlen(sep);
	if (seplen <= 0) {
		fprintf(stderr, "seperator string length 0\n");
		return -1;
	}
	buf[0] = str;
	int i = 1;
	char *ptr;
	while ((ptr = strstr(str, sep))) {
		*ptr = '\0';
		if (i >= bufn) {
			return i;
		}
		str = ptr + seplen;
		buf[i] = str;
		i++;
	}
	return i;
}

void strarrprint(const char *arr[], const size_t len) {
	for (size_t i = 0; i < len; i++) {
		if (arr[i] == NULL) {
			printf("(null)\n");
			continue;
		}
		printf("%s\n", arr[i]);
	}
}

int parse_http_request(char* s, struct HttpRequest* h) {
	const size_t len = strlen(s);
	if (len <= 0 || len > REQUEST_MAX_SIZE_BYTES) {
		fprintf(stderr, "size (%lu) bytes of http request is out of range (0 - %i)\n", len, REQUEST_MAX_SIZE_BYTES);
		return -1;
	}
	char *p[2];
	int n = strstrtok(s, "\n\n", p, 2);
	if (n < 2) {
		fprintf(stderr, "http request ill formed");
		return -1;
	}
	h->body = p[1];
	char *j[REQUEST_HEADER_FIELDS_LIMIT];
	int i = strstrtok(p[0], "\n", j, REQUEST_HEADER_FIELDS_LIMIT);
}

int main(void){
	char str[] = "hello this is a string\n\nasdf";
	struct HttpRequest h;
	parse_http_request(str, &h);
	return 0;
}
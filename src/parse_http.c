#include <stdio.h>
#include <string.h>
#include <sys/signal.h>

#include "server.h"
#include "parse_http.h"
#include "log.h"

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

// modifies str in place and places pointers into buf
ssize_t str_split(char* str, const char* sep, char* buf[], const size_t bufn) {
	if (str == NULL) {
		lprintf(ERROR, "str argument in %s() is NULL", __func__);
		return -1;
	}
	if (sep == NULL) {
		lprintf(ERROR, "sep argument in %s() is NULL", __func__);
		return -1;
	}
	if (bufn == 0) {
		lprintf(ERROR, __func__, "buffer size 0 in %s()");
		return -1;
	}
	const size_t sep_len = strlen(sep);
	if (sep_len == 0) {
		lprintf(ERROR, "seperator string length 0");
		return -1;
	}
	buf[0] = str;
	size_t i = 1;
	char* ptr;
	while ((ptr = strstr(str, sep))) {
		*ptr = '\0';
		if (i >= bufn) {
			return (ssize_t)i;
		}
		str = ptr + sep_len;
		buf[i] = str;
		i++;
	}
	return (ssize_t)i;
}

void str_arr_print(const char* arr[], const size_t len) {
	for (size_t i = 0; i < len; i++) {
		printf("%s\n", arr[i]);
	}
}

// in s, replace all a with b
int strrepl(char* s, const char a, const char b) {
	if (s == NULL) {
		return -1;
	}
	for (; *s; s++) {
		if (*s == a) {
			*s = b;
		}
	}
	return 0;
}

int parse_http_headers(char* s, struct HttpHeaders* h) {
	char* j[REQUEST_HEADER_FIELDS_LIMIT];
	ssize_t i = str_split(s, "\r\n", j, REQUEST_HEADER_FIELDS_LIMIT);
	for (int r = 0; r < i; r++) {
		char* c[2] = {0};
		str_split(j[r], ": ", c, 2);
		h->fields[r].key = c[0];
		h->fields[r].value = c[1];
		//printf("%s: %s\n", h[r].key, h[r].value);
	}
	h->nfields = (size_t) i;
	return 0;
}

int parse_http_body(char* str, struct HttpBody* body) {
	char* buf[2];
	ssize_t n = str_split(str, "\r\n\r\n", buf, 2); // split body and headers
	if (n != 2) {
		lprintf(ERROR, "ill formed http request");
		return -1;
	}
	if (*buf[1] == '\0') {
		body->ptr = NULL;
	} else {
		body->ptr = (void *) buf[1];
	}
	body->len = 0;
	return 0;
}

enum HttpMethod parse_http_method(const char* s) {
	typedef struct {
		const char* s_method;
		const enum HttpMethod enum_method;
	} table_t;
	const table_t table[] = {
		{"OPTIONS", HTTP_METHOD_OPTIONS},
		{"GET", HTTP_METHOD_GET},
		{"HEAD", HTTP_METHOD_HEAD},
		{"POST", HTTP_METHOD_POST},
		{"PUT", HTTP_METHOD_PUT},
		{"DELETE", HTTP_METHOD_DELETE},
		{"TRACE", HTTP_METHOD_TRACE},
		{"CONNECT", HTTP_METHOD_CONNECT}
	};
	const size_t len = ARR_LEN(table);
	for (size_t i = 0; i < len; i++) {
		if (strcmp(table[i].s_method, s) == 0) {
			return table[i].enum_method;
		}
	}
	return HTTP_METHOD_UNKNOWN;
}

// TODO: allow http binary data

enum HttpVersion parse_http_version(const char* s) {
	typedef struct {
		const char* s_version;
		const enum HttpVersion enum_version;
	} table_t;
	const table_t table[] = {
		{"HTTP/1.0", HTTP_VERSION_1_0},
		{"HTTP/1.1", HTTP_VERSION_1_1}
	};
	size_t len = ARR_LEN(table);
	for (size_t i = 0; i < len; i++) {
		if (strcmp(table[i].s_version, s) == 0) {
			return table[i].enum_version;
		}
	}
	return HTTP_VERSION_UNKNOWN;
}

int parse_http_request_line(char* str, struct HttpRequestLine* request_line) {
	char* buf[3];
	ssize_t n = str_split(str, " ", buf, 3);
	if (n != 3) {
		return -1;
	}
	request_line->method = parse_http_method(buf[0]);
	if (request_line->method == HTTP_METHOD_UNKNOWN) {
		return -1;
	}
	request_line->uri = buf[1];
	request_line->version = parse_http_version(buf[2]);
	if (request_line->version == HTTP_VERSION_UNKNOWN) {
		return -1;
	}
	return 0;
}

void print_http_request_struct(const struct HttpRequest* request) {
	printf("method enum: \"%i\"\n", request->request_line.method);
	printf("uri: \"%s\"\n", request->request_line.uri);
	printf("version enum: \"%i\"\n", request->request_line.version);
	for (int i = 0; i < REQUEST_HEADER_FIELDS_LIMIT; i++) {
		if (request->headers.fields[i].key == NULL && request->headers.fields[i].value == NULL) {
			break;
		}
		printf("key: \"%s\", value: \"%s\"\n", request->headers.fields[i].key, request->headers.fields[i].value);
	}
}

void print_http_response_struct(const struct HttpResponse* response) {
	printf("version enum: \"%i\"\n", response->status_line.version);
	printf("status code: \"%i\"\n", response->status_line.status_code);
	printf("reason phrase: \"%s\"\n", response->status_line.reason_phrase);
	for (int i = 0; i < REQUEST_HEADER_FIELDS_LIMIT; i++) {
		if (response->headers.fields[i].key == NULL && response->headers.fields[i].value == NULL) {
			break;
		}
		printf("key: \"%s\", value: \"%s\"\n", response->headers.fields[i].key, response->headers.fields[i].value);
	}
}

int parse_http_request(char* s, struct HttpRequest* h) {
	const size_t len = strlen(s);
	if (len <= 0 || len > REQUEST_MAX_SIZE_BYTES) {
		lprintf(ERROR, "size (%lu) bytes of http request is out of range (0 - %i)", len, REQUEST_MAX_SIZE_BYTES);
		return -1;
	}
	if (parse_http_body(s, &h->body) == -1) {
		return -1;
	}
	char* ptr_header_start; {
		char* tmp_ptr = strstr(s, "\r\n");
		*tmp_ptr = '\0';
		ptr_header_start = tmp_ptr + 2;
	}
	parse_http_headers(ptr_header_start, &h->headers);
	parse_http_request_line(s, &h->request_line);
	return 0;
}

char* get_http_header(const char* key, const struct HttpHeaders* headers) {
	const size_t len = headers->nfields;
	for (size_t i = 0; i < len; i++) {
		if (strcmp(key, headers->fields[i].key) == 0) {
			return headers->fields[i].value;
		}
	}
	return NULL;
}


int set_http_field(const char* key, const char* value, struct HttpHeaders* headers) {
	const size_t len = headers->nfields;
	for (size_t i = 0; i < len; i++) {
		if (strcmp(key, headers->fields[i].key) == 0) {
			headers->fields[i].value = (char *) value;
			return 0;
		}
	}
	headers->fields[len].key = (char *) key;
	headers->fields[len].value = (char *) value;
	headers->nfields++;
	return 0;
}

// i wish you could do something like this in c
// FileHandlerRAII file = fopen("path");
// closes on dealloc
// ~FileHandlerRAII() {
//   if (fclose(m_fp) == -1) throw err
// }

#include <stdio.h>
#include <string.h>
#include <sys/signal.h>

#include "main.h"
#include "parse_http.h"

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))

int str_split(char *str, const char *sep, char *buf[], const size_t bufn) {
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
	unsigned i = 1;
	char *ptr;
	while ((ptr = strstr(str, sep))) {
		*ptr = '\0';
		if (i >= bufn) {
			return (int)i;
		}
		str = ptr + seplen;
		buf[i] = str;
		i++;
	}
	return (int)i;
}

void str_arr_print(const char *arr[], const size_t len) {
	for (size_t i = 0; i < len; i++) {
		printf("%s\n", arr[i]);
	}
}

// in s, replace all a with b
int strrepl(char *s, const char a, const char b) {
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

int parse_http_headers(char *s, struct HttpHeaders *h) {
	char *j[REQUEST_HEADER_FIELDS_LIMIT];
	int i = str_split(s, "\r\n", j, REQUEST_HEADER_FIELDS_LIMIT);
	for (int r = 0; r < i; r++) {
		char *c[2] = {0};
		str_split(j[r], ": ", c, 2);
		h->fields[r].key = c[0];
		h->fields[r].value = c[1];
		//printf("%s: %s\n", h[r].key, h[r].value);
	}
	h->nfields = (size_t)i;
	return 0;
}

int parse_http_body(char *str, struct HttpBody *body) {
	char *buf[2];
	int n = str_split(str, "\r\n\r\n", buf, 2); // split body and headers
	if (n != 2) {
		fprintf(stderr, "ill formed http request\n");
		return -1;
	}
	if (*buf[1] == '\0') {
		body->ptr = NULL;
	} else {
		body->ptr = (void*)buf[1];
	}
	body->len = 0;
	return 0;
}

enum HttpMethod parse_http_method(const char *s) {
	typedef union {
		char *s_method;
		enum HttpMethod enum_method;
	} table_t;
	table_t table[][2] = {
		{{"OPTIONS"}, {.enum_method = HTTP_METHOD_OPTIONS}},
		{{"GET"}, {.enum_method = HTTP_METHOD_GET}},
		{{"HEAD"}, {.enum_method =  HTTP_METHOD_HEAD}},
		{{"POST"}, {.enum_method = HTTP_METHOD_POST}},
		{{"PUT"}, {.enum_method = HTTP_METHOD_PUT}},
		{{"DELETE"}, {.enum_method = HTTP_METHOD_DELETE}},
		{{"TRACE"}, {.enum_method = HTTP_METHOD_TRACE}},
		{{"CONNECT"}, {.enum_method = HTTP_METHOD_CONNECT}}
	};
	size_t len = ARR_LEN(table);
	for (size_t i = 0; i < len; i++) {
		if (strcmp(table[i][0].s_method, s) == 0) {
			return table[i][1].enum_method;
		}
	}
	return HTTP_METHOD_UNKNOWN;
}
// TODO: allow http binary data

enum HttpVersion parse_http_version(const char *s) {
	typedef union {
		char *s_version;
		enum HttpVersion enum_version;
	} table_t;
	table_t table[][2] = {
		{{"HTTP/1.0"}, {.enum_version = HTTP_VERSION_1_0}},
		{{"HTTP/1.1"}, {.enum_version = HTTP_VERSION_1_1}}
	};
	size_t len = ARR_LEN(table);
	for (size_t i = 0; i < len; i++) {
		if (strcmp(table[i][0].s_version, s) == 0) {
			return table[i][1].enum_version;
		}
	}
	return HTTP_VERSION_UNKNOWN;
}

int parse_http_request_line(char *str, struct HttpRequestLine *request_line) {
	char *buf[3];
	int n = str_split(str, " ", buf, 3);
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

void print_http_request_struct(const struct HttpRequest *request) {
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

int parse_http_request(char* s, struct HttpRequest* h) {
	const size_t len = strlen(s);
	if (len <= 0 || len > REQUEST_MAX_SIZE_BYTES) {
		fprintf(stderr, "size (%lu) bytes of http request is out of range (0 - %i)\n", len, REQUEST_MAX_SIZE_BYTES);
		return -1;
	}
	if (parse_http_body(s, &h->body) == -1) {
		return -1;
	}
	char *ptr_header_start;
	{
		char *tmp_ptr = strstr(s, "\r\n");
		*tmp_ptr = '\0';
		ptr_header_start = tmp_ptr + 2;
	}
	parse_http_headers(ptr_header_start, &h->headers);
	parse_http_request_line(s, &h->request_line);
	return 0;
}

char *get_http_header(const char *key, const struct HttpHeaders *headers) {
	const size_t len = headers->nfields;
	for (size_t i = 0; i < len; i++) {
		if (strcmp(key, headers->fields[i].key) == 0) {
			return headers->fields[i].value;
		}
	}
	return NULL;
}

// i wish you could do something like this in c
// FileHandlerRAII file = fopen("path");
// closes on dealloc
// ~FileHandlerRAII() {
//   if (fclose(m_fp) == -1) throw err
// }

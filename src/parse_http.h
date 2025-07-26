#ifndef PARSE_HTTP_H
#define PARSE_HTTP_H

#define REQUEST_HEADER_FIELDS_LIMIT 100
#define HEADER_EXISTS(key, header) (get_http_header(key, header) != NULL)
#define HEADER_EQ(key, header, value) (HEADER_EXISTS(key, header) && STR_EQ(get_http_header(key, header), value))

enum HttpMethod {
	HTTP_METHOD_OPTIONS, HTTP_METHOD_GET, HTTP_METHOD_HEAD, HTTP_METHOD_POST, HTTP_METHOD_PUT, HTTP_METHOD_DELETE,
	HTTP_METHOD_TRACE, HTTP_METHOD_CONNECT, HTTP_METHOD_UNKNOWN
};

enum HttpVersion {
	HTTP_VERSION_1_0, HTTP_VERSION_1_1, HTTP_VERSION_2, HTTP_VERSION_3, HTTP_VERSION_UNKNOWN
};

struct HttpRequestLine {
	enum HttpMethod method;
	char *uri;
	enum HttpVersion version;
};

struct HttpField {
	char *key;
	char *value;
};

typedef struct HttpHeaders {
	size_t nfields;
	struct HttpField fields[REQUEST_HEADER_FIELDS_LIMIT];
} headers_t;

struct HttpBody {
	void *ptr;
	size_t len;
};

struct HttpStatusLine {
	enum HttpVersion version;
	int status_code;
	char *reason_phrase;
};

struct HttpRequest {
	headers_t headers;
	struct HttpBody body;
	struct HttpRequestLine request_line;
};

struct HttpResponse {
	headers_t headers;
	struct HttpBody body;
	struct HttpStatusLine status_line;
};

struct HttpGeneric {
	headers_t headers;
	struct HttpBody body;
};

struct HttpStorage {
	struct HttpHeaders headers;
};

int parse_http_request(char* s, struct HttpRequest* h);
void print_http_request_struct(const struct HttpRequest *request);
void print_http_response_struct(const struct HttpResponse *response);
char *get_http_header(const char *key, const headers_t *headers);
int set_http_field(char *key, char *value, headers_t *headers);

#endif //PARSE_HTTP_H

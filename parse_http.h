#ifndef PARSE_HTTP_H
#define PARSE_HTTP_H

#define REQUEST_HEADER_FIELDS_LIMIT 100

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

struct HttpHeaders {
	size_t nfields;
	struct HttpField fields[REQUEST_HEADER_FIELDS_LIMIT];
};

struct HttpBody {
	void *ptr;
	size_t len;
};

struct HttpRequest {
	struct HttpRequestLine request_line;
	struct HttpHeaders headers;
	struct HttpBody body;
};


struct HttpStatusLine {
	enum HttpMethod method;
	int status_code;
	char *reason_phrase;
};

struct HttpResponse {
	struct HttpStatusLine status_line;
	struct HttpField fields[100];
	struct HttpBody body;
};

int parse_http_request(char* s, struct HttpRequest* h);
void print_http_request_struct(struct HttpRequest *request);
char *get_http_header(const char *key, const struct HttpHeaders *headers);

#endif //PARSE_HTTP_H

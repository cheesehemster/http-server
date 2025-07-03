#define REQUEST_LINE_URI_LIMIT 1024
#define REQUEST_HEADER_FIELDS_LIMIT 100
#define REQUEST_HEADER_FIELD_SIZE 1024

enum HttpMethod {

};

enum HttpVersion {

};

struct HttpRequest {
	enum HttpMethod method;
	char uri[REQUEST_LINE_URI_LIMIT];
	enum HttpVersion version;
	char fields[2][REQUEST_HEADER_FIELD_SIZE][REQUEST_HEADER_FIELDS_LIMIT];
};

int parse_http_request(const char *s, struct HttpRequest *h) {
	
}
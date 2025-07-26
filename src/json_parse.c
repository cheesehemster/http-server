#include "json_parse.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char *ptr;
	size_t size; // size includes null terminator
	size_t len; // size doenst include null terminator
	size_t capacity;
	size_t min_size;
} dynam_str;

int dynam_str_init(dynam_str *ds, const char *str) {
	if (ds == NULL || str == NULL) {
		fprintf(stderr, "null arg ptr\n");
		return -1;
	}
	ds->min_size = 16;
	const size_t len = strlen(str);
	const size_t len_min = (len + 1) < ds->min_size ? ds->min_size - 1 : len;
	ds->capacity = len_min + 1;
	ds->ptr = malloc(ds->capacity);
	if (ds->ptr == NULL) {
		perror("malloc failed");
		return -1;
	}
	ds->len = len;
	ds->size = len_min + 1;
	strcpy(ds->ptr, str);
	return 0;
}

int dynam_str_resize(dynam_str *ds, const size_t size) {
	ds->ptr = realloc(ds->ptr, size);
	if (ds->ptr == NULL) {
		return -1;
	}
	ds->capacity = size;
	return 0;
}

int dynam_str_append_char(dynam_str *ds, const char c) {
	while (ds->size + 1 >= ds->capacity) {
		dynam_str_resize(ds, ds->size * 2);
	}
	ds->ptr[ds->len] = c;
	ds->ptr[ds->len + 1] = '\0';
	ds->len++;
	ds->size++;
	return 0;
}

int dynam_str_append_str(dynam_str *ds, const char *str) {
	const size_t len = strlen(str);
	while (ds->size + len >= ds->capacity) {
		dynam_str_resize(ds, ds->size * 2);
	}
	ds->len += len;
	ds->size += len;
	strcpy(ds->ptr + len, str);
	return 0;
}

int dynam_str_free(dynam_str *ds) {
	free(ds);
	return 0;
}

struct key_value;

typedef struct {
	struct key_value *pairs;
} json_object;

typedef struct {
	json_value *elements;
} json_array;

typedef struct {
	char *str;
} json_string;

enum json_number_type {
	INT,
	FLOAT
};

typedef struct {
	enum json_number_type type ;
	union {
		int i;
		double f;
	} val;
} json_number;

enum value_t {
	OBJECT,
	ARRAY,
	STRING,
	NUMBER
};


struct json_value {
	enum value_t value_t;
	union {
		json_object value_object;
		json_array value_array;
		json_string value_string;
		json_number value_number;
	} json_value;
};

struct key_value {
	json_string key;
	json_value value;
};

int json_parse_string(const char *str, int *i, json_value *s) {
	if (str == NULL || i == NULL || s == NULL) {
		fprintf(stderr, "null arg ptr\n");
		return -1;
	}
	dynam_str *chars = malloc(sizeof(dynam_str));
	dynam_str_init(chars, "");
	size_t int_i = 0;
	assert(str[*i] == '"');
	(*i)++;
	while (true) {
		if (str[*i] == '"') {
			(*i)++;
			break;
		}
		if (str[*i] == '\\') {
			(*i)++;
			switch (str[*i]) {
				case '"':
					dynam_str_append_char(chars, '"');
					break;
				case '\\':
					dynam_str_append_char(chars, '\\');
					break;
				case '/':
					dynam_str_append_char(chars, '/');
					break;
				case 'b':
					dynam_str_append_char(chars, '\b');
					break;
				case 'f':
					dynam_str_append_char(chars, '\f');
					break;
				case 'n':
					dynam_str_append_char(chars, '\n');
					break;
				case 'r':
					dynam_str_append_char(chars, '\r');
					break;
				case 'u':
					dynam_str_append_char(chars, 'u');
					break;
				default:
					fprintf(stderr, "errrr brr\n");
					return -1;
			}
		} else {
			if (str[*i] >= 32 && str[*i] <= 126) {
				dynam_str_append_char(chars, str[*i]);
			} else {
				return -1;
			}
		}
		(*i)++;
		int_i++;
	}
	dynam_str_append_char(chars, '\0');
	s->json_value.value_string.str = chars->ptr;
	return 0;
}

int json_parse_whitespace(const char *str, int *i) {
	while (true) {
		switch (str[*i]) {
			case 9:
			case 13:
			case 10:
			case 32:
				(*i)++;
				break;
			default:
				return 0;
		}
	}
}

char *json_parse_value(const char *str, int *i) {
	json_parse_whitespace(str, i);
	(*i) += 1;
	json_parse_whitespace(str, i);
	return (char*)1;
}


int json_parse_object(const char *str, int *i, json_value *v) {
	assert(str[*i] == '{');
	(*i)++;
	json_parse_whitespace(str, i);
	if (str[*i] == '}') {
		printf("empty object\n");
		return 0;
	}
	while (true) {
		json_parse_whitespace(str, i);
		json_value sdf;
		if (json_parse_string(str, i, &sdf) == -1) {
			return -1;
		}
		json_parse_whitespace(str, i);
		assert(("expected ':'", str[*i] == ':'));
		(*i)++;
		if (json_parse_value(str, i) == NULL) {
			return -1;
		}
		switch (str[*i]) {
			case ',':
				break;
			case '}':
				printf("parsed successuflly!\n");
				return 0;
			default:
				fprintf(stderr, "problem brr\n");
				return -1;
		}
	}
}

json_array *json_parse_array(const char *str, int *i) {
	assert(str[*i] == '[');
	json_parse_whitespace(str, i);
	if (str[*i] == ']') {
		printf("empty array\n");
		return NULL;
	}
	while (true) {
		json_value m;
		json_parse_object(str, i, &m);
		switch (str[*i]) {
			case ',':
				break;
			case ']':
				return NULL;
			default:
				fprintf(stderr, "brr\n");
				return NULL;
		}
	}
}

int main(void) {
	int i = 0;
	const char *str = "{\"egg\": 1}";
	const char *jsonstr = "\"hello this is \\njson string\"";
	printf("%s\n", jsonstr);
	int j = 0;
	json_value s;
	json_parse_string(jsonstr, &j, &s);
	printf("%s\n", s.json_value.value_string.str);
	//json_parse_object(str, &i);
}

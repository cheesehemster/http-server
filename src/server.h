#ifndef MAIN_H
#define MAIN_H

#define REQUEST_MAX_SIZE_BYTES (1000000 * 1) // 1mb

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define STRING_EQ(a, b) (strcmp(a, b) == 0)

int run_server(void);
void print_str_no_cr(const char* str);

#endif

cmake_minimum_required(VERSION 3.31)
project(http_server C)

set(CMAKE_C_STANDARD 23)

add_compile_options(
        #-Werror
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
        -Wredundant-decls -Wunreachable-code -Winit-self
        -Wmissing-declarations -Wformat=2 -Wfloat-equal
        -Wswitch-default -Wswitch-enum -Wcast-align
        -Wctor-dtor-privacy
        -Wdisabled-optimization
)

add_executable(http_server src/main.c src/server.c src/parse_http.c src/log.c)
#add_executable(http_server test.c)

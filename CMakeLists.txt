cmake_minimum_required(VERSION 3.31)
project(http_server C)

set(CMAKE_C_STANDARD 23)

add_compile_options(
        #-Werror
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
        -Wredundant-decls -Wunreachable-code -Winit-self
        -Wmissing-declarations -Wformat=2 -Wfloat-equal
        -Wswitch-enum -Wcast-align
        -Wctor-dtor-privacy
        #-Wdisabled-optimization
        #-Wswitch-default
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")

include_directories(src)

set(TEST_FILE 1)

if (TEST_FILE)
    add_executable(http_server
            src/json_parse.c
            src/json_parse.h
    )
else()
    add_executable(http_server
            src/main.c

            src/server.c
            src/server.h

            src/parse_http.c
            src/parse_http.h

            src/log.c
            src/log.h

            src/thread_pool.c
            src/thread_pool.h
            src/json_parse.c
            src/json_parse.h
    )
endif()

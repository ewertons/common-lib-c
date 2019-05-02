project(circular_list_ut)

cmake_minimum_required(VERSION 3.0.2)

set(header_files
)

set(source_files
    ./main.c
)

add_executable(circular_list_ut ${header_files} ${source_files})

include_directories(${COMMON_LIB_C_INCLUDE_DIR})

target_link_libraries(circular_list_ut
    common-lib-c 
)



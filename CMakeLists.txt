project(common-lib-c)

cmake_minimum_required(VERSION 3.0.2)

set(header_files
    ./inc/circular_list.h
    ./inc/stringx.h
)

set(source_files
    ./src/circular_list.c
    ./src/stringx.c
)

add_library(common-lib-c ${header_files} ${source_files})

include_directories(./inc)

target_link_libraries(common-lib-c 
)

set(COMMON_LIB_C_INC_DIR ${CMAKE_CURRENT_LIST_DIR}/inc CACHE INTERNAL "")

add_subdirectory(tests)


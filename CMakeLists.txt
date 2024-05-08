cmake_minimum_required(VERSION 3.16.0)

project(common-lib-c)

find_package(cmocka CONFIG REQUIRED)

enable_testing()

set(header_files
    ${CMAKE_CURRENT_LIST_DIR}/inc/circular_list.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/stringx.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/span.h
)

set(source_files
    ${CMAKE_CURRENT_LIST_DIR}/src/circular_list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/stringx.c
    ${CMAKE_CURRENT_LIST_DIR}/src/span.c
    ${CMAKE_CURRENT_LIST_DIR}/src/bst_redblack.c
    ${CMAKE_CURRENT_LIST_DIR}/src/stack.c
    ${CMAKE_CURRENT_LIST_DIR}/src/list.c
)

add_library(common-lib-c ${header_files} ${source_files})

set(COMMON_LIB_C_INC_DIR ${CMAKE_CURRENT_LIST_DIR}/inc CACHE INTERNAL "")

target_include_directories (common-lib-c
  PUBLIC
  $<BUILD_INTERFACE:${COMMON_LIB_C_INC_DIR}>
  $<INSTALL_INTERFACE:include/common-lib-c>
)

add_subdirectory(tests)

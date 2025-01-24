cmake_minimum_required(VERSION 3.16.0)

project(common-lib-c)

find_package(cmocka CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

enable_testing()

set(header_files
    ${CMAKE_CURRENT_LIST_DIR}/inc/ansi_colors.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/base64.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/bst_redblack.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/circular_list.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/common_lib_c.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/hmac_sha256.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/list.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/logging_simple.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/niceties.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/socket.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/socket_stream.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/span.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/stack.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/stream.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/stringx.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/task.h
)

set(source_files
    ${CMAKE_CURRENT_LIST_DIR}/src/base64.c
    ${CMAKE_CURRENT_LIST_DIR}/src/bst_redblack.c
    ${CMAKE_CURRENT_LIST_DIR}/src/circular_list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/hmac_sha256.c
    ${CMAKE_CURRENT_LIST_DIR}/src/list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/socket.c
    ${CMAKE_CURRENT_LIST_DIR}/src/socket_stream.c
    ${CMAKE_CURRENT_LIST_DIR}/src/span.c
    ${CMAKE_CURRENT_LIST_DIR}/src/stack.c
    ${CMAKE_CURRENT_LIST_DIR}/src/stringx.c
    ${CMAKE_CURRENT_LIST_DIR}/src/task.c
)

add_library(common-lib-c ${header_files} ${source_files})

set(COMMON_LIB_C_INC_DIR ${CMAKE_CURRENT_LIST_DIR}/inc CACHE INTERNAL "")

target_include_directories (common-lib-c
  PUBLIC
  $<BUILD_INTERFACE:${COMMON_LIB_C_INC_DIR}>
  $<INSTALL_INTERFACE:include/common-lib-c>
)

target_link_libraries(
    common-lib-c
      ${OPENSSL_LIBRARIES}
)

add_subdirectory(tests)

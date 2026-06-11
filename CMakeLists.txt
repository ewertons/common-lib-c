cmake_minimum_required(VERSION 3.16.0)

project(common-lib-c)

option(COMMON_LIB_C_BUILD_TESTS   "Build test executables (requires cmocka)" ON)

# ---------------------------------------------------------------------------
# Portability switches.
#
#   COMMON_LIB_C_TLS_BACKEND        OPENSSL (default) | NONE
#       NONE drops the OpenSSL dependency entirely (plain TCP only). Required
#       on targets without OpenSSL such as ESP-IDF (mbedTLS) or bare lwIP.
#
#   COMMON_LIB_C_EVENT_LOOP_BACKEND AUTO (default) | EPOLL | SELECT
#       AUTO selects epoll on Linux and select() elsewhere. SELECT forces the
#       portable select() backend (e.g. lwIP, which lacks epoll/eventfd).
# ---------------------------------------------------------------------------
set(COMMON_LIB_C_TLS_BACKEND "OPENSSL" CACHE STRING "TLS backend: OPENSSL or NONE")
set_property(CACHE COMMON_LIB_C_TLS_BACKEND PROPERTY STRINGS OPENSSL NONE)

set(COMMON_LIB_C_EVENT_LOOP_BACKEND "AUTO" CACHE STRING "Event loop backend: AUTO, EPOLL or SELECT")
set_property(CACHE COMMON_LIB_C_EVENT_LOOP_BACKEND PROPERTY STRINGS AUTO EPOLL SELECT)

if(${COMMON_LIB_C_BUILD_TESTS})
    find_package(cmocka CONFIG REQUIRED)
endif()

if(COMMON_LIB_C_TLS_BACKEND STREQUAL "OPENSSL")
    find_package(OpenSSL REQUIRED)
elseif(COMMON_LIB_C_TLS_BACKEND STREQUAL "NONE")
    message(STATUS "common-lib-c: TLS backend disabled (SOCKET_TLS_NONE)")
else()
    message(FATAL_ERROR "COMMON_LIB_C_TLS_BACKEND must be OPENSSL or NONE, got '${COMMON_LIB_C_TLS_BACKEND}'")
endif()

enable_testing()

set(header_files
    ${CMAKE_CURRENT_LIST_DIR}/inc/ansi_colors.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/arg_parser.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/base64.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/bst_redblack.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/circular_list.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/common_lib_c.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/event_loop.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/hmac_sha256.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/json.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/list.h
    ${CMAKE_CURRENT_LIST_DIR}/inc/logging.h
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
    ${CMAKE_CURRENT_LIST_DIR}/src/arg_parser.c
    ${CMAKE_CURRENT_LIST_DIR}/src/base64.c
    ${CMAKE_CURRENT_LIST_DIR}/src/bst_redblack.c
    ${CMAKE_CURRENT_LIST_DIR}/src/circular_list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/event_loop.c
    ${CMAKE_CURRENT_LIST_DIR}/src/hmac_sha256.c
    ${CMAKE_CURRENT_LIST_DIR}/src/json_reader.c
    ${CMAKE_CURRENT_LIST_DIR}/src/json_token.c
    ${CMAKE_CURRENT_LIST_DIR}/src/json_writer.c
    ${CMAKE_CURRENT_LIST_DIR}/src/list.c
    ${CMAKE_CURRENT_LIST_DIR}/src/logging.c
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

if(COMMON_LIB_C_TLS_BACKEND STREQUAL "NONE")
    target_compile_definitions(common-lib-c PUBLIC SOCKET_TLS_NONE=1)
    target_link_libraries(common-lib-c m)
else()
    target_link_libraries(common-lib-c ${OPENSSL_LIBRARIES} m)
endif()

if(COMMON_LIB_C_EVENT_LOOP_BACKEND STREQUAL "SELECT")
    target_compile_definitions(common-lib-c PUBLIC EVENT_LOOP_BACKEND_SELECT=1)
elseif(COMMON_LIB_C_EVENT_LOOP_BACKEND STREQUAL "EPOLL")
    target_compile_definitions(common-lib-c PUBLIC EVENT_LOOP_BACKEND_EPOLL=1)
endif()

if(${COMMON_LIB_C_BUILD_TESTS})
    add_subdirectory(tests)
endif()

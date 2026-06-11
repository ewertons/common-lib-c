# Reusable ESP-IDF component for common-lib-c.
#
# An ESP-IDF component is named after its directory, so this folder is
# deliberately named "common-lib-c": the component name is therefore always
# "common-lib-c" regardless of the repository checkout directory name.
#
# To consume this component from an ESP-IDF project, add this directory's
# parent (the repo's "components" folder) to EXTRA_COMPONENT_DIRS and list
# "common-lib-c" in your component's REQUIRES, e.g.:
#
#   set(EXTRA_COMPONENT_DIRS path/to/common-lib-c/components)
#   # in your component CMakeLists.txt: REQUIRES common-lib-c
#
# Only the portable subset is compiled: TLS is disabled (no OpenSSL on
# ESP-IDF; the rest of the system uses mbedTLS) and the portable select()
# event-loop backend is selected (lwIP has no epoll/eventfd).

# common-lib-c repository root, two levels up from this file:
#   components/common-lib-c -> repo root
get_filename_component(COMMON_LIB_C_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

idf_component_register(
    SRCS
        "${COMMON_LIB_C_ROOT}/src/arg_parser.c"
        "${COMMON_LIB_C_ROOT}/src/base64.c"
        "${COMMON_LIB_C_ROOT}/src/bst_redblack.c"
        "${COMMON_LIB_C_ROOT}/src/circular_list.c"
        "${COMMON_LIB_C_ROOT}/src/event_loop.c"
        "${COMMON_LIB_C_ROOT}/src/hmac_sha256.c"
        "${COMMON_LIB_C_ROOT}/src/json_reader.c"
        "${COMMON_LIB_C_ROOT}/src/json_token.c"
        "${COMMON_LIB_C_ROOT}/src/json_writer.c"
        "${COMMON_LIB_C_ROOT}/src/list.c"
        "${COMMON_LIB_C_ROOT}/src/logging.c"
        "${COMMON_LIB_C_ROOT}/src/socket.c"
        "${COMMON_LIB_C_ROOT}/src/socket_stream.c"
        "${COMMON_LIB_C_ROOT}/src/span.c"
        "${COMMON_LIB_C_ROOT}/src/stackx.c"
        "${COMMON_LIB_C_ROOT}/src/stringx.c"
        "${COMMON_LIB_C_ROOT}/src/task.c"
    INCLUDE_DIRS
        "${COMMON_LIB_C_ROOT}/inc"
    REQUIRES
        lwip
        pthread
)

# Propagate the portability switches (PUBLIC) so dependents that include the
# common-lib-c headers compile against the same configuration.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    SOCKET_TLS_NONE=1
    EVENT_LOOP_BACKEND_SELECT=1
)

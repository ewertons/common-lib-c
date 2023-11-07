cmake_minimum_required (VERSION 3.10)

set(TARGET_NAME "common_lib_c_tests")

project (${TARGET_NAME} LANGUAGES C)

set(CMAKE_C_STANDARD 99)

find_package(cmocka CONFIG REQUIRED)

enable_testing()

include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/inc
    ${CMOCKA_PUBLIC_INCLUDE_DIRS}
)

add_executable(${TARGET_NAME}
    ${CMAKE_CURRENT_LIST_DIR}/src/main.c
    ${CMAKE_CURRENT_LIST_DIR}/src/test_circular_list.c
)

target_link_libraries(${TARGET_NAME} common-lib-c cmocka)

add_test(${TARGET_NAME} ${TARGET_NAME})

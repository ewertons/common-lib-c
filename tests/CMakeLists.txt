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

file(GLOB TEST_SOURCE_FILES ${CMAKE_CURRENT_LIST_DIR}/src/*.c)

message(STATUS TEST_SOURCE_FILES=${TEST_SOURCE_FILES})

add_executable(${TARGET_NAME}
    ${TEST_SOURCE_FILES}
)

target_link_libraries(${TARGET_NAME} common-lib-c cmocka ssl crypto pthread)

add_test(${TARGET_NAME} ${TARGET_NAME})

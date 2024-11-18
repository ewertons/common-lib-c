#ifndef RESULT_H
#define RESULT_H

#define sizeofarray(x) (sizeof(x)/sizeof(x[0]))
#define strlitlen(S) (sizeof(S "") - 1)

#define OK       0
#define ERROR    __LINE__

#define C_UTILS 0
#define is_success_result 0
#define is_error_result 1
#define is_retryable_error 1
#define is_non_retryable_error 0

#define make_result(component, is_failure, is_retryable, code) \
    ((component << 18) +  (is_retryable << 17) + (is_failure << 16)  + code)

typedef enum result
{
    ok =                make_result(C_UTILS, is_success_result, is_retryable_error,     0x00),
    invalid_argument =  make_result(C_UTILS, is_error_result,   is_non_retryable_error, 0x01),
    insufficient_size = make_result(C_UTILS, is_error_result,   is_non_retryable_error, 0x02),
    not_found =         make_result(C_UTILS, is_error_result,   is_non_retryable_error, 0x03),
    end_of_data =       make_result(C_UTILS, is_success_result, is_non_retryable_error, 0x05),
    end_of_file =       make_result(C_UTILS, is_success_result, is_non_retryable_error, 0x06),
    try_again =         make_result(C_UTILS, is_error_result,   is_retryable_error,     0x10),
    completed_successfully =         make_result(C_UTILS, is_success_result,   is_retryable_error,     0x20),
    error =             make_result(C_UTILS, is_error_result,   is_non_retryable_error, 0xFF)
} result_t;

#define result_get_error_flag(r) ((r >> 16) & 0x01)

#define is_error(r) (result_get_error_flag(r) == is_error_result)
#define failed(r)   is_error(r)
#define is_success(r) (result_get_error_flag(r) == is_success_result)
#define succeeded(r)   is_success(r)

#endif // RESULT_H

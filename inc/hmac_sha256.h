#ifndef COMMON_LIB_C_HMAC_SHA256_H
#define COMMON_LIB_C_HMAC_SHA256_H

#include "span.h" 

void sha_hash_to_hex_string(span_t hash, span_t string, span_t* out_string);

int hmac_sha256_get_hash(span_t key, span_t data, span_t hash, span_t* out_hash);

#endif // COMMON_LIB_C_HMAC_SHA256_H

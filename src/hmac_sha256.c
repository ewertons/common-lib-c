#include "hmac_sha256.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

void sha_hash_to_hex_string(span_t hash, span_t string, span_t* out_string)
{
    int i;
    uint8_t* string_raw = span_get_ptr(string);

    for(i = 0; i < span_get_size(hash); i++)
    {
        sprintf(string_raw + (i * 2), "%02x", span_get(hash, i));
    }

    string_raw[i * 2] = 0;

    *out_string = span_slice(string, 0, i * 2); // do not comput \0 in size.
}

int hmac_sha256_get_hash(span_t key, span_t data, span_t hash, span_t* out_hash)
{
    int result;
    unsigned int hash_length;
 
    char* msg_auth_code = HMAC(
        EVP_sha256(), span_get_ptr(key), span_get_size(key), span_get_ptr(data), span_get_size(data), span_get_ptr(hash), &hash_length);
    
    if (msg_auth_code == NULL)
    {
        result = ERROR;
    }
    else
    {
        if (out_hash != NULL)
        {
            *out_hash = span_slice(hash, 0, hash_length);
        }

        result = 0;
    }

    return result;
}

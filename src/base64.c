#include "base64.h"

#include <stdio.h>

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};
static int mod_table[] = {0, 2, 1};

int base64_encode(span_t data, span_t encoded, span_t* out_encoded)
{
    int32_t encoded_size = 4 * ((span_get_size(data) + 2) / 3);
    
    if (span_get_size(encoded) < encoded_size)
    {
        return ERROR;
    }

    uint8_t* encoded_raw = span_get_ptr(encoded);

    for (int i = 0, j = 0; i < span_get_size(data);)
    {
        uint32_t octet_a = i < span_get_size(data) ? span_get(data, i++) : 0;
        uint32_t octet_b = i < span_get_size(data) ? span_get(data, i++) : 0;
        uint32_t octet_c = i < span_get_size(data) ? span_get(data, i++) : 0;
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_raw[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_raw[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_raw[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_raw[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    // Add padding if necessary
    for (int i = 0; i < mod_table[span_get_size(data) % 3]; i++)
    {
        encoded_raw[encoded_size - 1 - i] = '=';
    }

    if (out_encoded != NULL)
    {
        *out_encoded = span_slice(encoded, 0, encoded_size);
    }

    return 0;
}

// Values 'A' to 'Z', 'a' to 'z', '0' to '9', '+' and '/' map to
// 0 to 25, 26 to 51, 52 to 61, 62 and 63 respectively.
static uint32_t decode_base64_value(uint8_t c)
{
    return ((c >= 'A' && c <= 'Z') ?
                (c - 'A'):
                (c >= 'a' && c <= 'z') ?
                    (c - 'a') + 26 :
                    (c >= '0' && c <= '9') ?
                        (c - '0') + 52 :
                            (c == '+') ? 62 :
                                (c == '/') ? 63 : 0);
}


int base64_decode(span_t data, span_t decoded, span_t* out_decoded)
{
    if (span_get_size(data) % 4 != 0)
    {
        return ERROR;
    }

    int32_t decoded_size = span_get_size(data) / 4 * 3;
    uint8_t* data_raw = span_get_ptr(data);
    uint8_t* decoded_raw = span_get_ptr(decoded);

    if (data_raw[span_get_size(data) - 1] == '=') (decoded_size)--;
    if (data_raw[span_get_size(data) - 2] == '=') (decoded_size)--;

    if (span_get_size(decoded) < decoded_size)
    {
        return ERROR;
    }

    for (int i = 0, j = 0; i < span_get_size(data);)
    {
        uint32_t sextet_a = data_raw[i] == '=' ? 0 & i++ : decode_base64_value(data_raw[i++]);
        uint32_t sextet_b = data_raw[i] == '=' ? 0 & i++ : decode_base64_value(data_raw[i++]);
        uint32_t sextet_c = data_raw[i] == '=' ? 0 & i++ : decode_base64_value(data_raw[i++]);
        uint32_t sextet_d = data_raw[i] == '=' ? 0 & i++ : decode_base64_value(data_raw[i++]);

        uint32_t triple = (sextet_a << 3 * 6)
            + (sextet_b << 2 * 6)
            + (sextet_c << 1 * 6)
            + (sextet_d << 0 * 6);

        if (j < decoded_size) decoded_raw[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < decoded_size) decoded_raw[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < decoded_size) decoded_raw[j++] = (triple >> 0 * 8) & 0xFF;
    }

    if (out_decoded != NULL)
    {
        *out_decoded = span_slice(decoded, 0, decoded_size);
    }

    return 0;
}

// static char *decoding_table = NULL;

// static void build_decoding_table() {

//     decoding_table = malloc(256);

//     for (int i = 0; i < 64; i++)
//         decoding_table[(unsigned char) encoding_table[i]] = i;
// }


// static void base64_cleanup() {
//     free(decoding_table);
// }

// int base64_decode(span_t data, span_t decoded, span_t* out_decoded)
// {
//     build_decoding_table();

//     if (span_get_size(data) % 4 != 0)
//     {
//         return ERROR;
//     }

//     int32_t decoded_size = span_get_size(data) / 4 * 3;
//     uint8_t* data_raw = span_get_ptr(data);
//     uint8_t* decoded_raw = span_get_ptr(decoded);

//     if (data_raw[span_get_size(data) - 1] == '=') (decoded_size)--;
//     if (data_raw[span_get_size(data) - 2] == '=') (decoded_size)--;

//     for (int i = 0, j = 0; i < span_get_size(data);) {

//         uint32_t sextet_a = data_raw[i] == '=' ? 0 & i++ : decoding_table[data_raw[i++]];
//         uint32_t sextet_b = data_raw[i] == '=' ? 0 & i++ : decoding_table[data_raw[i++]];
//         uint32_t sextet_c = data_raw[i] == '=' ? 0 & i++ : decoding_table[data_raw[i++]];
//         uint32_t sextet_d = data_raw[i] == '=' ? 0 & i++ : decoding_table[data_raw[i++]];

//         uint32_t triple = (sextet_a << 3 * 6)
//             + (sextet_b << 2 * 6)
//             + (sextet_c << 1 * 6)
//             + (sextet_d << 0 * 6);

//         if (j < decoded_size) decoded_raw[j++] = (triple >> 2 * 8) & 0xFF;
//         if (j < decoded_size) decoded_raw[j++] = (triple >> 1 * 8) & 0xFF;
//         if (j < decoded_size) decoded_raw[j++] = (triple >> 0 * 8) & 0xFF;
//     }

//     if (out_decoded != NULL)
//     {
//         *out_decoded = span_slice(decoded, 0, decoded_size);
//     }

//     base64_cleanup();

//     return 0;
// }

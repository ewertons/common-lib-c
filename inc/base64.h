#ifndef BASE64_H
#define BASE64_H

#include <stdint.h>
#include <stdlib.h>

#include "span.h"

int base64_encode(span_t data, span_t encoded, span_t* out_encoded);
int base64_decode(span_t data, span_t decoded, span_t* out_decoded);
void base64_test();

#endif // BASE64_H
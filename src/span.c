#include <string.h>

#include <span.h>

#include <regex.h>

#define isdigit(x) (x >= '0' && x <= '9')

#define REGMATCH_ARRAY_MAX_SIZE 10

int span_set(span_t span, uint32_t position, uint8_t value)
{
    int result;

    if (span_is_empty(span) || position >= span_get_size(span))
    {
        result = ERROR;
    }
    else
    {
        span.ptr[position] = value;
        result = OK;
    }

    return result;
}

int span_compare(span_t a, span_t b)
{
    int result;

    if (a.ptr == NULL && b.ptr == NULL)
    {
        result = 0;
    }
    else if (a.ptr == NULL && b.ptr != NULL)
    {
        result = 1;
    }
    else if (a.ptr != NULL && b.ptr == NULL)
    {
        result = -1;
    }
    else
    {
        if (a.length != b.length)
        {
            result = b.length - a.length;
        }
        else
        {
            result = 0;

            for (uint32_t x = 0; x < a.length; x++)
            {
                if (a.ptr[x] < b.ptr[x])
                {
                    result = 1;
                    break;
                }
                else if (a.ptr[x] > b.ptr[x])
                {
                    result = -1;
                    break;
                }
            }            
        }
    }

    return result;
}

int span_find(span_t span, int32_t start, span_t target, span_t* out_remainder)
{
    int result = -1;

    if (start >=0 && span_get_size(target) > 0 && (span_get_size(span) - start) >= span_get_size(target))
    {
        for (int32_t h = start; h <= (span_get_size(span) - span_get_size(target)); h++)
        {
            if (span_get(span, h) == span_get(target, 0))
            {
                uint32_t i;
                for (i = 1; i < span_get_size(target); i++)
                {
                    if (span_get(span, h + i) != span_get(target, i))
                    {
                        i = 0;
                        break;
                    }
                }

                if (i > 0)
                {
                    *out_remainder = span_slice_to_end(span, h + span_get_size(target)); 
                    result = h;
                    break;
                }
            }
        }   
    }

    return result;
}

result_t span_iterate(span_t span, span_t delimiter, span_t* out_item, span_t* remainder)
{
    result_t result;

    if (span_is_empty(delimiter) || out_item == NULL || remainder == NULL)
    {
        result = invalid_argument;
    }
    else if (span_is_empty(span))
    {
        result = end_of_data;
    }
    else
    {
        span_t find_remainder;
        int position = span_find(span, 0, delimiter, &find_remainder);

        if (position == -1)
        {
            *out_item = span;
            *remainder = SPAN_EMPTY;
            result = ok;
        }
        else
        {
            *out_item = span_slice(span, 0, position);
            *remainder = find_remainder;
            result = ok;
        }
    }

    return result;
}

int span_split(span_t span, uint32_t start, span_t delimiter, span_t* left, span_t* right)
{
    int result;
    span_t remainder;
    int position = span_find(span, start, delimiter, &remainder);

    if (position == -1)
    {
        result = ERROR;
    }
    else
    {
        if (left != NULL)
        {
            *left = span_slice(span, start, position - start);
        }

        if (right != NULL)
        {
            *right = remainder;
        }

        result = OK;
    }

    return result;
}

int span_to_uint32_t(span_t span, uint32_t* value)
{
    int result;

    if (value == NULL)
    {
        result = ERROR;
    }
    else
    {
        uint32_t l = span_get_size(span);

        if (l > 10 || (l == 10 && span_get(span, 0) > '4'))
        {
            result = ERROR;
        }
        else
        {
            uint32_t v = 0, m = 1, i = l;

            do
            {
                uint8_t c = span_get(span, --i);

                if (!isdigit(c))
                {
                    result = ERROR;
                    goto SPAN_TO_UINT32_T_FUNCTION_RETURN;
                }

                v += (c - '0') * m;
                m *= 10;
            } while (i > 0);

            *value = v;
            result = OK;
        }
    }

SPAN_TO_UINT32_T_FUNCTION_RETURN:
    return result;
}

span_t span_copy_int32(span_t span, int32_t value, span_t* out_span)
{
    span_t result;

    if (span_is_empty(span))
    {
        result = SPAN_EMPTY;
    }
    else
    {
        span_t original_span = span;

        int mask = 1000000000;

        if (value < 0)
        {
            if (span_copy_u8(span, '-', &span))
            {
                result = SPAN_EMPTY;
                goto SPAN_FROM_UINT32_FUNCTION_RETURN;
            }

            mask = (-1) * mask;
        }

        if (value == 0)
        {
            mask = 1;
        }
        else
        {
            while ((value % mask) == value)
            {
                mask /= 10;
            }
        }

        while (mask != 0)
        {
            int digit = value / mask;
            value = value % mask;
            mask /= 10;
            
            if (span_copy_u8(span, '0' + digit, &span) != 0)
            {
                result = SPAN_EMPTY;
                goto SPAN_FROM_UINT32_FUNCTION_RETURN;
            }
        }

        result = span_slice(original_span, 0, span_get_size(original_span) - span_get_size(span));

        if (out_span != NULL)
        {
            *out_span = span;
        }
    }

SPAN_FROM_UINT32_FUNCTION_RETURN:
    return result;
}

span_t span_copy(span_t to, span_t from, span_t* remainder)
{
    if (span_get_size(to) == 0 || span_get_size(to) < span_get_size(from))
    {
        return SPAN_EMPTY;
    }

    (void)memcpy(span_get_ptr(to), span_get_ptr(from), span_get_size(from));

    if (remainder != NULL)
    {
        *remainder = span_slice_to_end(to, span_get_size(from));
    }

    return span_slice(to, 0, span_get_size(from));
}

int span_copy_u8(span_t to, uint8_t c, span_t* remainder)
{
    int result;

    if (span_is_empty(to))
    {
        result = ERROR;
    }
    else
    {
        if (span_set(to, 0, c) != 0)
        {
            result = ERROR;
        }
        else
        {
            if (remainder != NULL)
            {
                *remainder = span_slice_to_end(to, 1);
            }

            result = OK;
        }
    }

    return result;
}

span_t span_copy_n(span_t to, span_t* from, int32_t count, int32_t* required_size, span_t* remainder)
{
    if (from == NULL || count < 1)
    {
        return SPAN_EMPTY;
    }

    int32_t total_size = 0;
    for (int i = 0; i < count; i++)
    {
        total_size += span_get_size(from[i]);
    }

    if (required_size != NULL)
    {
        *required_size = total_size;
    }

    if (span_get_size(to) < total_size)
    {
        return SPAN_EMPTY;
    }

    span_t to_remainder = to;
    for (int i = 0; i < count; i++)
    {
        (void)span_copy(to_remainder, from[i], &to_remainder);
    }

    if (remainder != NULL)
    {
        *remainder = to_remainder;
    }

    return span_slice(to, 0, total_size);
}

result_t span_regex_is_match(span_t string, span_t pattern, span_t* matches, uint16_t size_of_matches, uint16_t* number_of_matches)
{
    result_t result;

    if (span_is_empty(string) || span_is_empty(pattern) || 
        matches == NULL && (size_of_matches > 0 || number_of_matches != NULL) ||
        matches != NULL && (size_of_matches == 0 || size_of_matches > REGMATCH_ARRAY_MAX_SIZE || number_of_matches == NULL))
    {
        result = invalid_argument;
    }
    else if (!span_is_null_terminated(string) || !span_is_null_terminated(pattern))
    {
        result = invalid_argument;
    }
    else
    {
        regex_t regex;
        regoff_t off, len;

        if (regcomp(&regex, span_get_ptr(pattern), REG_EXTENDED))
        {
            result = error;
        }
        else
        {
            regmatch_t pmatches[REGMATCH_ARRAY_MAX_SIZE];

            if (regexec(&regex, span_get_ptr(string), sizeofarray(pmatches), pmatches, 0))
            {
                result = not_found;
            }
            else
            {
                if (size_of_matches > 0)
                {
                    *number_of_matches = 0;

                    for (int i = 0; i < size_of_matches && i < REGMATCH_ARRAY_MAX_SIZE; i++)
                    {
                        if (pmatches[i].rm_so == -1) break;

                        matches[i] = span_slice(string, pmatches[i].rm_so, pmatches[i].rm_eo - pmatches[i].rm_so);
                        (*number_of_matches)++;
                    }
                }

                result = ok;
            }

            regfree(&regex);
        }
    }

    return result;
}

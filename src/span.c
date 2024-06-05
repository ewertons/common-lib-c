#include <span.h>
#include <string.h>

#define isdigit(x) (x >= '0' && x <= '9')

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

int span_find(span_t span, int32_t start, span_t target, span_t* out_found)
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
                    *out_found = span_slice(span, h, span_get_size(target)); 
                    result = h;
                    break;
                }
            }
        }   
    }

    return result;
}

int span_split(span_t span, uint32_t start, span_t delimiter, span_t* left, span_t* right)
{
    int result = span_find(span, start, delimiter, &delimiter);

    if (result != -1)
    {
        *left = span_slice(span, start, result - start);
        *right = span_slice_to_end(span, result + span_get_size(delimiter));
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

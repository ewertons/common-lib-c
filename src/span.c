#include <span.h>

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

int span_find(span_t* span, uint32_t start, span_t target, uint32_t* pos)
{
    int result = -1;

    if ((span->length - start) >= target.length)
    {
        for (uint32_t h = start; h <= (span_get_length(span) - span_get_length(&target)); h++)
        {
            if (span_get(span, h) == span_get(&target, 0))
            {
                uint32_t i;
                for (i = 1; i < span_get_length(&target); i++)
                {
                    if (span_get(span, h + i) != span_get(&target, i))
                    {
                        i = 0;
                        break;
                    }
                }

                if (i > 0)
                {
                    *pos = h;
                    result = 0;
                    break;
                }
            }
        }   
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
        uint32_t l = span_get_length(&span);

        if (l > 10 || (l == 10 && span_get(&span, 0) > '4'))
        {
            result = ERROR;
        }
        else
        {
            uint32_t v = 0, m = 1, i = l;

            do
            {
                uint8_t c = span_get(&span, --i);

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

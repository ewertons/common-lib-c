#include "stringx.h"
#include <string.h>

int stringx_clone(char** dst, const char* src)
{
	int result;
	
	if (dst == NULL || src == NULL)
	{
		result = __LINE__;
	}
	else
	{
		int length = strlen(src) + 1;
		
		if ((*dst = malloc(sizeof(char) * length)) == NULL)
		{
			result = __LINE__;
		}
		else
		{
			memcpy(*dst, src, length);
			result = 0;
		}
	}
	
	return result;
}

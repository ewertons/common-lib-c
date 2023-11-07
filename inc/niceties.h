#ifndef RESULT_H
#define RESULT_H

#define sizeofarray(x) (sizeof(x)/sizeof(x[0]))
#define strlitlen(S) (sizeof(S "") - 1)

#define OK       0
#define ERROR    __LINE__

#endif // RESULT_H

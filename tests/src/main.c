#include <stdlib.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include <test_circular_list.h>

int main()
{
  int result = 0;

  result += test_circular_list();

  return result;
}

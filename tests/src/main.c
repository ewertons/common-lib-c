#include <stdlib.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include <tests.h>

int main()
{
  int result = 0;

  result += test_span();
  result += test_circular_list();
  result += test_bst_redblack();
  result += test_stack();

  return result;
}

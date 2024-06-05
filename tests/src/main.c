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
  result += test_list();
  result += test_base64();
  result += test_sha256();

  return result;
}

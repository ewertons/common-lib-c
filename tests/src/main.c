#include <stdlib.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include <tests.h>
#include "task.h"


int main()
{
  int result = 0;

  task_platform_init();

  result += test_span();
  result += test_circular_list();
  result += test_bst_redblack();
  result += test_stack();
  result += test_list();
  result += test_base64();
  result += test_hmac_sha256();
  result += test_socket();
  
  // result += test_task();

  task_platform_deinit();

  return result;
}

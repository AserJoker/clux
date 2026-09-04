#include "cmd/test.h"
#include <stdio.h>

int cmd_test(const cmd_args_t *args) {
  (void)args;
  fprintf(stderr, "test: not implemented\n");
  return 1;
}

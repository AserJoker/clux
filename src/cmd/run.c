#include "cmd/run.h"
#include "driver/driver.h"
#include <stdio.h>

int cmd_run(const cmd_args_t *args) {
  if (!args) return 1;

  /* The source file is the first positional argument. */
  const char *path = cmd_args_pos(args, 0);
  if (!path) {
    fprintf(stderr, "run: missing input file\n");
    fprintf(stderr, "usage: clux run <file.cx>\n");
    return 1;
  }

  return driver_run_file(path);
}

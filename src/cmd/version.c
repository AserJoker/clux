#include "cmd/version.h"
#include "icu_data.h"
#include <stdio.h>
#include <unicode/uchar.h>
#include <unicode/utypes.h>

int cmd_version(const cmd_args_t *args) {
  (void)args;

  if (icu_data_init() != 0) {
    fprintf(stderr, "failed to initialize ICU common data\n");
    return 1;
  }

  UVersionInfo ver;
  u_getVersion(ver);
  char ver_str[U_MAX_VERSION_STRING_LENGTH];
  u_versionToString(ver, ver_str);
  printf("clux 0.1.0\n");
  printf("ICU version: %s\n", ver_str);
  return 0;
}

#include <stdio.h>

#include "cmd/cmd.h"
#include "unicode/uchar.h"
#include "unicode/utypes.h"
#include "icu_data.h"

/* ---- "format" subcommand ---- */

static int cmd_format(const cmd_args_t *args) {
  (void)args;
  fprintf(stderr, "format: not implemented\n");
  return 1;
}

/* ---- "build" subcommand ---- */

static int cmd_build(const cmd_args_t *args) {
  (void)args;
  fprintf(stderr, "build: not implemented\n");
  return 1;
}

/* ---- "run" subcommand ---- */

static int cmd_run(const cmd_args_t *args) {
  (void)args;
  fprintf(stderr, "run: not implemented\n");
  return 1;
}

/* ---- "test" subcommand ---- */

static int cmd_test(const cmd_args_t *args) {
  (void)args;
  fprintf(stderr, "test: not implemented\n");
  return 1;
}

/* ---- "version" subcommand ---- */

static int cmd_version(const cmd_args_t *args) {
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

/* ---- Command table ---- */

static const cmd_t g_cmds[] = {
    {
        .name = "format",
        .usage = "clux format [options]",
        .help = "Format source code.",
        .handler = cmd_format,
    },
    {
        .name = "build",
        .usage = "clux build [options]",
        .help = "Build the project.",
        .handler = cmd_build,
    },
    {
        .name = "run",
        .usage = "clux run [options]",
        .help = "Run the project.",
        .handler = cmd_run,
    },
    {
        .name = "test",
        .usage = "clux test [options]",
        .help = "Run tests.",
        .handler = cmd_test,
    },
    {
        .name = "version",
        .usage = "clux version",
        .help = "Print version information.",
        .handler = cmd_version,
    },
};

#define NUM_CMDS (sizeof(g_cmds) / sizeof(g_cmds[0]))

/* ---- main ---- */

int main(int argc, char *argv[]) {
  return cmd_dispatch(g_cmds, NUM_CMDS, argc, argv);
}

#include "cmd/cmd.h"
#include "cmd/format.h"
#include "cmd/build.h"
#include "cmd/run.h"
#include "cmd/test.h"
#include "cmd/version.h"

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

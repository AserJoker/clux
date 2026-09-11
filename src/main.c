#include "cmd/cmd.h"
#include "cmd/format.h"
#include "cmd/build.h"
#include "cmd/run.h"
#include "cmd/test.h"
#include "cmd/version.h"
#include "cmd/eval.h"
#include "icu_data.h"
#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#endif

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
    {
        .name = "eval",
        .usage = "clux eval <expr>",
        .help = "Evaluate an expression at compile time (CTFE).",
        .handler = cmd_eval,
    },
};

#define NUM_CMDS (sizeof(g_cmds) / sizeof(g_cmds[0]))

/* ---- UTF-8 locale setup (cross-platform) ---- */

/* Force the C runtime to treat text as UTF-8 so output is not mis-encoded
 * on terminals that default to a local code page (notably Windows cmd). */
static void setup_utf8_locale(void) {
#ifdef _WIN32
  setlocale(LC_ALL, ".UTF-8");
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#else
  /* Try an explicit UTF-8 locale first, then fall back to whatever the
   * environment provides; if all fail we leave the C locale as-is. */
  if (!setlocale(LC_ALL, "C.UTF-8") &&
      !setlocale(LC_ALL, "en_US.UTF-8")) {
    setlocale(LC_ALL, "");
  }
#endif
}

/* ---- main ---- */

int main(int argc, char *argv[]) {
  setup_utf8_locale();

  /* Register the embedded ICU data before any ICU API (lexer's
   * identifier / grapheme-cluster handling) is touched. */
  icu_data_init();
  return cmd_dispatch(g_cmds, NUM_CMDS, argc, argv);
}

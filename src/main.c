#include "cmd/cmd.h"
#include "cmd/format.h"
#include "cmd/build.h"
#include "cmd/run.h"
#include "cmd/bc.h"
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
        .usage = "clux build <file.cx>",
        .help =
            "Compile clux source into a native machine-code binary.\n"
            "\n"
            "Not implemented yet (planned: M7 C backend / M12 native backend).\n"
            "For bytecode artifacts, use `clux bc`.",
        .handler = cmd_build,
    },
    {
        .name = "run",
        .usage = "clux run <file>",
        .help =
            "Run a clux program.\n"
            "\n"
            "The input kind is decided by CONTENT, not by extension:\n"
            "  - starts with the CXBC binary header -> run as bytecode\n"
            "  - otherwise                           -> compile as clux source",
        .handler = cmd_run,
    },
    {
        .name = "bc",
        .usage = "clux bc <emit|asm|disasm> <file> [-o PATH]",
        .help =
            "Bytecode tools (non-mainline; debug / distribution).\n"
            "\n"
            "  emit    <file.cx>   compile source into .cxb binary bytecode\n"
            "  asm     <file.cxs>  assemble text assembly into .cxb binary\n"
            "  disasm  <file.cxb>  disassemble binary bytecode into .cxs text\n"
            "\n"
            "PATH omitted => same name as input with the new extension.",
        .handler = cmd_bc,
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

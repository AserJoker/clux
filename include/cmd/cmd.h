#ifndef _H_CLUX_CMD_CMD_
#define _H_CLUX_CMD_CMD_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/* ---- Parsed arguments ---- */

/**
 * A single key-value option parsed from --key=value or --key.
 * If the option is a flag (--key without =value), value is NULL.
 */
typedef struct {
  const char *key;   /* without "--" prefix, e.g. "verbose" for --verbose */
  const char *value; /* NULL for flags, the value string otherwise */
} cmd_opt_t;

/**
 * Parsed command-line arguments: options + positional args.
 * All pointers point into the original argv — no allocation or copying.
 */
typedef struct {
  const cmd_opt_t *opts;  /* array of parsed options */
  size_t optc;            /* number of options */
  char *const *posargs;   /* positional arguments (argv slots) */
  size_t posc;            /* number of positional arguments */
} cmd_args_t;

/**
 * Parse argc/argv into a cmd_args_t.
 * The output struct and its opt array are stack/static-friendly;
 * no heap allocation is performed. The `opts` array is written into
 * the caller-provided buffer `opt_buf` of size `opt_buf_len`.
 *
 * Recognizes:
 *   --key=value   → opts[i] = {key="key", value="value"}
 *   --key         → opts[i] = {key="key", value=NULL}
 *   other         → positional argument
 *
 * Returns the number of options parsed (cannot exceed opt_buf_len).
 */
size_t cmd_args_parse(int argc, char **argv, cmd_opt_t *opt_buf,
                      size_t opt_buf_len, cmd_args_t *out);

/**
 * Look up an option value by key name (without the "--" prefix).
 * Returns NULL if the key was not present, or the value string if it was.
 * For flag-style options (--verbose), use cmd_args_has instead.
 */
const char *cmd_args_get(const cmd_args_t *args, const char *key);

/**
 * Return true if the given option key was present (with or without value).
 */
bool cmd_args_has(const cmd_args_t *args, const char *key);

/**
 * Return the i-th positional argument (0-indexed), or NULL if out of bounds.
 */
const char *cmd_args_pos(const cmd_args_t *args, size_t i);

/* ---- Command descriptor ---- */

/**
 * Handler function for a subcommand.
 * `args` contains the parsed options and positional arguments.
 * Returns an exit code (0 for success).
 */
typedef int (*cmd_handler_fn_t)(const cmd_args_t *args);

typedef struct {
  const char *name;        /* subcommand name */
  const char *usage;       /* one-line usage, e.g. "clux demo [options]" */
  const char *help;        /* multi-line description printed by --help */
  cmd_handler_fn_t handler;
} cmd_t;

/* ---- Top-level dispatch ---- */

/**
 * Print top-level usage to stdout, listing all subcommands.
 * `prog` is the program name (typically argv[0]).
 */
void cmd_print_help(const char *prog, const cmd_t *cmds, size_t ncmds);

/**
 * Dispatch to the appropriate subcommand based on argv[1].
 * If argv[1] is "--help" or "-h", prints top-level help and returns 0.
 * If argv[1] matches a subcommand name, parses remaining arguments and
 * calls its handler. If the subcommand receives --help, the handler
 * should print its own help and return 0.
 * Returns the exit code from the handler, or 1 on error.
 */
int cmd_dispatch(const cmd_t *cmds, size_t ncmds, int argc, char **argv);

#ifdef __cplusplus
}
#endif
#endif

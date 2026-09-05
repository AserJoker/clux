#include "cmd/cmd.h"
#include <stdio.h>
#include <string.h>

/* ---- cmd_args implementation ---- */

size_t cmd_args_parse(int argc,
                      char **argv,
                      cmd_opt_t *opt_buf,
                      size_t opt_buf_len,
                      cmd_args_t *out) {
  if (!out) {
    if (opt_buf && opt_buf_len > 0) opt_buf[0].key = NULL;
    return 0;
  }

  out->opts = opt_buf;
  out->optc = 0;
  out->posargs = argv;
  out->posc = 0;

  /* Count positional args first by shifting them to the front of posargs */
  /* We'll set posargs to point at the first positional arg in argv */
  size_t pos_start = 0; /* index into argv where positional args begin */

  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];

    if (arg[0] == '-' && arg[1] == '-') {
      const char *key_start = arg + 2;
      if (*key_start == '\0') continue; /* bare "--", skip */

      if (out->optc < opt_buf_len) {
        const char *eq = strchr(key_start, '=');
        if (eq) {
          size_t key_len = (size_t)(eq - key_start);
          /* argv strings are modifiable per C standard */
          ((char *)key_start)[key_len] = '\0';
          opt_buf[out->optc].key = key_start;
          opt_buf[out->optc].value = eq + 1;
        } else {
          opt_buf[out->optc].key = key_start;
          opt_buf[out->optc].value = NULL;
        }
        out->optc++;
      }
    } else {
      /* Positional argument: record its position */
      if (pos_start == 0 && out->posc == 0) pos_start = (size_t)i;
      out->posc++;
    }
  }

  out->posargs = (pos_start > 0 || out->posc > 0) ? &argv[pos_start] : argv;
  /* Adjust: if no opts, posargs start at 0 */
  if (out->optc == 0) {
    out->posargs = argv;
    out->posc = (size_t)argc;
  } else {
    out->posargs = &argv[pos_start];
    /* posc was already counted */
  }

  /* Null-terminate opt array for safety */
  if (opt_buf && opt_buf_len > out->optc) {
    opt_buf[out->optc].key = NULL;
    opt_buf[out->optc].value = NULL;
  }

  return out->optc;
}

const char *cmd_args_get(const cmd_args_t *args, const char *key) {
  if (!args || !key) return NULL;
  for (size_t i = 0; i < args->optc; i++) {
    if (strcmp(args->opts[i].key, key) == 0) return args->opts[i].value;
  }
  return NULL;
}

bool cmd_args_has(const cmd_args_t *args, const char *key) {
  if (!args || !key) return false;
  for (size_t i = 0; i < args->optc; i++) {
    if (strcmp(args->opts[i].key, key) == 0) return true;
  }
  return false;
}

const char *cmd_args_pos(const cmd_args_t *args, size_t i) {
  if (!args || i >= args->posc) return NULL;
  return args->posargs[i];
}

/* ---- Top-level dispatch ---- */

void cmd_print_help(const char *prog, const cmd_t *cmds, size_t ncmds) {
  printf("Usage: %s <command> [options]\n\n", prog ? prog : "clux");
  printf("Commands:\n");
  for (size_t i = 0; i < ncmds; i++) {
    printf("  %-12s %s\n", cmds[i].name, cmds[i].usage);
  }
  printf("\nOptions:\n");
  printf("  --help       Show this help message\n");
}

int cmd_dispatch(const cmd_t *cmds, size_t ncmds, int argc, char **argv) {
  if (argc < 2) {
    cmd_print_help(argv[0], cmds, ncmds);
    return 1;
  }

  const char *sub = argv[1];

  /* Top-level --help */
  if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0) {
    cmd_print_help(argv[0], cmds, ncmds);
    return 0;
  }

  /* Find subcommand */
  const cmd_t *cmd = NULL;
  for (size_t i = 0; i < ncmds; i++) {
    if (strcmp(sub, cmds[i].name) == 0) {
      cmd = &cmds[i];
      break;
    }
  }

  if (!cmd) {
    fprintf(stderr, "unknown command: %s\n", sub);
    fprintf(stderr, "Run '%s --help' for usage.\n", argv[0]);
    return 1;
  }

  /* Parse remaining arguments (skip program name and subcommand) */
  cmd_opt_t opt_buf[32];
  cmd_args_t args;
  cmd_args_parse(argc - 2, argv + 2, opt_buf, 32, &args);

  /* If --help requested for the subcommand, print its help */
  if (cmd_args_has(&args, "help")) {
    printf("Usage: %s\n\n%s\n", cmd->usage, cmd->help);
    return 0;
  }

  return cmd->handler(&args);
}

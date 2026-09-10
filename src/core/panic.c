#include "core/panic.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef _WIN32
#include <process.h> /* _exit */
#endif

/* ---- Global panic handler (default: abort) ---- */

static panic_handler_t g_panic_handler = panic_handler_abort;

void set_panic_handler(panic_handler_t handler) {
  if (handler) g_panic_handler = handler;
}

panic_handler_t get_panic_handler(void) { return g_panic_handler; }

/* ---- Default handler ---- */

void panic_handler_abort(const char *message) {
  fprintf(stderr, "panic: %s\n", message);
#ifdef _WIN32
  /* Windows 上 abort() 会触发 CRT 错误对话框（尤其 debug 构建），
     改为快速退出：无 atexit 清理、无 core dump、不弹窗 */
  _exit(EXIT_FAILURE);
#else
  abort(); /* POSIX：保留 abort 以产生 core dump 便于调试 */
#endif
}

/* ---- panic ---- */

void panic(const char *fmt, ...) {
  char buf[1024];

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  buf[sizeof(buf) - 1] = '\0';
  g_panic_handler(buf);
}

#ifndef _H_CLUX_CORE_PANIC_
#define _H_CLUX_CORE_PANIC_
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Panic handler callback. Receives the fully formatted message.
 * The default handler calls abort(); it must NOT return.
 * A custom handler may throw (C++ exception) or longjmp instead.
 */
typedef void (*panic_handler_t)(const char *message);

/** Default handler — prints to stderr and calls abort(). */
void panic_handler_abort(const char *message);

/** Set/get the global panic handler. */
void set_panic_handler(panic_handler_t handler);
panic_handler_t get_panic_handler(void);

/**
 * Terminate the program with a formatted message.
 * Formats `fmt` with the trailing arguments, then invokes the current
 * panic_handler. By default the handler does not return.
 */
void panic(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

/* ---- assert macro -------------------------------------------------------- */

/* Undef the standard assert to avoid redefinition warnings. */
#ifdef assert
#undef assert
#endif

/**
 * assert(cond, msg)
 * If `cond` evaluates to false, panics with file/line info and `msg`.
 * `msg` should be a string literal or a const char*.
 */
#define assert(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      panic("%s:%d: assertion failed: (%s): %s", __FILE__, __LINE__, #cond,    \
            (msg));                                                            \
    }                                                                          \
  } while (0)

#endif

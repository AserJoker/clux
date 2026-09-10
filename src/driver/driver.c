#include "driver/driver.h"
#include "core/allocator.h"
#include "core/stream.h"
#include "core/vec.h"
#include "core/arena.h"
#include "core/string.h"
#include "parser/lexer.h"
#include "parser/location.h"
#include "parser/parser.h"
#include "parser/ast_node.h"
#include "parser/ast_kind.h"
#include "parser/ast_program.h"
#include "parser/ast_error.h"
#include "diag/diagnostic.h"
#include "sema/sema.h"
#include "sema/symbol.h"
#include "compiler/compiler.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "vm/scope.h"
#include "vm/exec.h"
#include "vm/bcode.h"
#include "vm/type_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Internal: byte buffer class (for file-backed source buffers) ---- */

static class_t g_bytes_class = {
    .name = "clux.driver.bytes",
    .size = sizeof(char),
    .clone_fn = default_clone,
    .move_fn = default_move,
    .dispose_fn = NULL,
};

/* ---- Stage ①: load a source file fully into an allocator buffer ---- */

int driver_load_source(allocator_t *alloc,
                       const char *path,
                       const char **out_data,
                       size_t *out_len) {
  if (!alloc || !path || !out_data || !out_len) return -1;

  *out_data = NULL;
  *out_len = 0;

  FILE *fp = fopen(path, "rb");
  if (!fp) return -1;

  /* Determine the file size via a seek to the end. */
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return -1;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return -1;
  }

  /* stream_source_mem requires a non-NULL data pointer, so allocate at
   * least one byte even for an empty file. */
  size_t n = (size_t)size;
  char *buf = (char *)allocator_new(alloc, &g_bytes_class, n > 0 ? n : 1);
  if (!buf) {
    fclose(fp);
    return -1;
  }

  size_t read = fread(buf, 1, n, fp);
  fclose(fp);

  if (read != n) {
    /* Short read: the buffer is owned by the allocator; the caller is
     * expected to hand it to a mechanism that frees it (e.g. an
     * owns_data memory source). Signal failure but leave the buffer
     * allocated so it is not leaked. */
    *out_data = buf;
    *out_len = read;
    return -1;
  }

  *out_data = buf;
  *out_len = n;
  return 0;
}

/* ---- Internal: load + lex into a pool, keeping the lexer alive ---- */

static int driver_lex_into(allocator_t *alloc,
                           const char *path,
                           vec_t **out_pool,
                           lexer_t **out_lexer) {
  const char *data = NULL;
  size_t len = 0;
  if (driver_load_source(alloc, path, &data, &len) != 0) return -1;

  stream_source_t src = stream_source_mem(alloc, data, len, /*owns_data=*/true);
  istream_t *stream = istream_open(alloc, src);
  if (!stream) return -1;

  lexer_t *lexer = lexer_create(alloc, stream, path);
  if (!lexer) {
    istream_close(&stream);
    return -1;
  }

  vec_t *pool = vec_new(alloc, /*owns_element=*/true);
  if (!pool) {
    lexer_close(&lexer);
    return -1;
  }

  for (;;) {
    token_t *t = lexer_next(lexer);
    if (!t) break;
    vec_push(pool, alloc, t);
    token_kind_t kind = token_get_kind(t);
    if (kind == TOKEN_TYPE_EOF) break;
    if (kind == TOKEN_TYPE_ERROR) {
      const location_t *loc = token_get_location(t);
      const char *msg = token_get_error_message(t);
      fprintf(stderr, "%s:%zu:%zu: error: %s\n",
              path,
              loc ? loc->begin.line : 0,
              loc ? loc->begin.column : 0,
              msg ? msg : "unrecognized input");
      lexer_close(&lexer);
      vec_free(alloc, &pool);
      return -2;
    }
  }

  *out_pool = pool;
  *out_lexer = lexer;
  return 0;
}

/* ---- Stage ② + ③ + ④: lex → parse → sema ---- */

int driver_run_file(const char *path) {
  if (!path) {
    fprintf(stderr, "run: no input file\n");
    return 1;
  }

  allocator_t *alloc = create_allocator(malloc, free);
  if (!alloc) {
    fprintf(stderr, "run: out of memory\n");
    return 1;
  }

  arena_t *arena = arena_new_default(alloc);
  if (!arena) {
    fprintf(stderr, "run: out of memory\n");
    delete_allocator(&alloc);
    return 1;
  }

  /* Stage ① + ②: 加载 → 词法分析（词法错误快速失败） */
  vec_t *pool = NULL;
  lexer_t *lexer = NULL;
  int lex_result = driver_lex_into(alloc, path, &pool, &lexer);
  if (lex_result == -2) {
    /* 词法错误已输出到 stderr */
    if (lexer) lexer_close(&lexer);
    if (pool) vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }
  if (lex_result != 0) {
    fprintf(stderr, "run: cannot open file '%s'\n", path);
    if (lexer) lexer_close(&lexer);
    if (pool) vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  /* Stage ③: 语法分析 */
  parser_t *parser = parser_create(alloc, arena, pool);
  if (!parser) {
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  ast_node_t *ast = parser_parse(parser);
  parser_destroy(&parser);

  if (!ast || ast->kind == AST_ERROR) {
    /* 语法错误，诊断已由 parser 输出 */
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  /* Stage ④: 语义分析（sema，语法通过后才进入；语义错误快速失败） */
  vm_t *vm = vm_new(alloc);
  if (!vm) {
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }
  diag_buf_t *diag = diag_buf_new(alloc);
  if (!diag) {
    vm_destroy(&vm);
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  sema_t *sema = sema_create(vm, diag, pool);
  if (!sema) {
    diag_buf_destroy(&diag);
    vm_destroy(&vm);
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  bool sema_ok = sema_analyze(sema, ast);
  /* 作用域树是持久化数据，编译器（AST → bcode）按名查找符号元数据，
     必须存活到编译完成；顺序参照生命周期约定：先取树、再销毁 sema、
     编译结束后再销毁树 */
  sema_scope_t *scope_tree = sema->global_scope;
  sema_destroy(&sema);

  if (!sema_ok) {
    /* 语义诊断已由 sema 记入 diag，统一在出口打印（diag 销毁前） */
    diag_print_all(diag);
    if (scope_tree) sema_scope_destroy(&scope_tree);
    diag_buf_destroy(&diag);
    vm_destroy(&vm);
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  /* Stage ⑤: 编译（AST + sema 作用域树 → 字节码模块） */
  compiler_t *comp = compiler_new(alloc, vm, diag, pool, scope_tree);
  if (!comp) {
    if (scope_tree) sema_scope_destroy(&scope_tree);
    diag_buf_destroy(&diag);
    vm_destroy(&vm);
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }
  bytecode_t *bc = compiler_compile(comp, ast);
  compiler_destroy(&comp);

  if (!bc) {
    /* 编译诊断已记入 diag，统一在出口打印 */
    diag_print_all(diag);
    if (scope_tree) sema_scope_destroy(&scope_tree);
    diag_buf_destroy(&diag);
    vm_destroy(&vm);
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  /* Stage ⑥: 执行（注册函数 → 调用 main） */
  value_t *er = exec_run(vm, bc);
  if (value_is_error(vm, er)) {
    error_data_t *ed = (error_data_t *)value_data(er);
    fprintf(stderr, "run: %s\n",
            ed && ed->message ? string_cstr(ed->message) : "execution error");
    bcode_destroy(&bc);
    if (scope_tree) sema_scope_destroy(&scope_tree);
    diag_buf_destroy(&diag);
    vm_destroy(&vm);
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  value_t *main_fn = scope_lookup(vm->current_scope, STRSLICE_LIT("main"));
  if (!main_fn) {
    fprintf(stderr, "run: no entry function 'main'\n");
    bcode_destroy(&bc);
    if (scope_tree) sema_scope_destroy(&scope_tree);
    diag_buf_destroy(&diag);
    vm_destroy(&vm);
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  value_t *mr = value_call(vm, main_fn, NULL, 0);
  if (value_is_error(vm, mr)) {
    error_data_t *ed = (error_data_t *)value_data(mr);
    fprintf(stderr, "run: %s\n",
            ed && ed->message ? string_cstr(ed->message) : "runtime error");
    bcode_destroy(&bc);
    if (scope_tree) sema_scope_destroy(&scope_tree);
    diag_buf_destroy(&diag);
    vm_destroy(&vm);
    lexer_close(&lexer);
    vec_free(alloc, &pool);
    arena_destroy(alloc, &arena);
    delete_allocator(&alloc);
    return 1;
  }

  bcode_destroy(&bc);
  if (scope_tree) sema_scope_destroy(&scope_tree);
  diag_buf_destroy(&diag);
  vm_destroy(&vm);

  lexer_close(&lexer);
  vec_free(alloc, &pool);
  arena_destroy(alloc, &arena);
  delete_allocator(&alloc);

  return 0;
}

/* ---- Test/utility entry: lex into a pool (text dangles after return) ---- */

int driver_lex_file(allocator_t *alloc, const char *path, vec_t **out_pool) {
  lexer_t *lexer = NULL;
  if (driver_lex_into(alloc, path, out_pool, &lexer) != 0) return -1;
  /* Closing the lexer frees the source buffer; token kinds/locations stay
   * valid but token text slices become dangling. Consumers needing the
   * text should keep the lexer alive (see driver_run_file). */
  lexer_close(&lexer);
  return 0;
}

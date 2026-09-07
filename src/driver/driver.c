#include "driver/driver.h"
#include "core/allocator.h"
#include "core/stream.h"
#include "core/vec.h"
#include "core/arena.h"
#include "parser/lexer.h"
#include "parser/location.h"
#include "parser/parser.h"
#include "parser/ast_node.h"
#include "parser/ast_kind.h"
#include "parser/ast_program.h"
#include "parser/ast_func_def.h"
#include "parser/ast_var_def.h"
#include "parser/ast_block.h"
#include "parser/ast_if.h"
#include "parser/ast_while.h"
#include "parser/ast_for.h"
#include "parser/ast_return.h"
#include "parser/ast_assign.h"
#include "parser/ast_expr_stmt.h"
#include "parser/ast_discard.h"
#include "parser/ast_ident.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_binary.h"
#include "parser/ast_unary.h"
#include "parser/ast_call.h"
#include "parser/ast_member.h"
#include "parser/ast_index.h"
#include "parser/ast_cast.h"
#include "parser/ast_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

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

/* ---- Stage ② + ③ + ④: lex → parse → output AST (JSON) ---- */

/** 输出 JSON 字符串（转义控制字符和双引号） */
static void print_json_string(const char *s, size_t len) {
  putchar('"');
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
    case '"':  fputs("\\\"", stdout); break;
    case '\\': fputs("\\\\", stdout); break;
    case '\b': fputs("\\b", stdout);  break;
    case '\f': fputs("\\f", stdout);  break;
    case '\n': fputs("\\n", stdout);  break;
    case '\r': fputs("\\r", stdout);  break;
    case '\t': fputs("\\t", stdout);  break;
    default:
      if (c < 0x20) printf("\\u%04X", c);
      else fputc(c, stdout);
    }
  }
  putchar('"');
}

/** 兄弟链节点数组的逗号分隔 */
static void print_json_siblings(const ast_node_t *head, int indent);

/** 将 AST 节点递归输出为 JSON，indent 控制缩进层级 */
static void print_ast_json(const ast_node_t *node, int indent) {
  if (!node) { fputs("null", stdout); return; }

  printf("{\"kind\":\"%s\"", ast_kind_name(node->kind));

  /* 类型特定字段 */
  switch (node->kind) {
  case AST_PROGRAM: {
    const ast_program_t *prog = (const ast_program_t *)node;
    printf(",\"funcs\":");
    print_json_siblings(prog->funcs, indent + 1);
    break;
  }
  case AST_FUNC_DEF: {
    const ast_func_def_t *fn = (const ast_func_def_t *)node;
    printf(",\"name\":");
    print_json_string(fn->name.ptr, fn->name.len);
    printf(",\"params\":");
    print_json_siblings(fn->params, indent + 1);
    if (fn->return_type.len > 0) {
      printf(",\"return_type\":");
      print_json_string(fn->return_type.ptr, fn->return_type.len);
    }
    printf(",\"body\":");
    print_ast_json(fn->body, indent + 1);
    break;
  }
  case AST_VAR_DEF: {
    const ast_var_def_t *vd = (const ast_var_def_t *)node;
    printf(",\"name\":");
    print_json_string(vd->name.ptr, vd->name.len);
    if (vd->type_name.len > 0) {
      printf(",\"type\":");
      print_json_string(vd->type_name.ptr, vd->type_name.len);
    }
    if (vd->init) {
      printf(",\"init\":");
      print_ast_json(vd->init, indent + 1);
    }
    break;
  }
  case AST_BLOCK: {
    const ast_block_t *blk = (const ast_block_t *)node;
    printf(",\"stmts\":");
    print_json_siblings(blk->stmts, indent + 1);
    break;
  }
  case AST_IF: {
    const ast_if_t *it = (const ast_if_t *)node;
    printf(",\"cond\":");
    print_ast_json(it->cond, indent + 1);
    printf(",\"then\":");
    print_ast_json(it->then_body, indent + 1);
    if (it->else_body) {
      printf(",\"else\":");
      print_ast_json(it->else_body, indent + 1);
    }
    break;
  }
  case AST_WHILE: {
    const ast_while_t *w = (const ast_while_t *)node;
    printf(",\"cond\":");
    print_ast_json(w->cond, indent + 1);
    printf(",\"body\":");
    print_ast_json(w->body, indent + 1);
    break;
  }
  case AST_FOR: {
    const ast_for_t *f = (const ast_for_t *)node;
    if (f->init) { printf(",\"init\":"); print_ast_json(f->init, indent + 1); }
    if (f->cond) { printf(",\"cond\":"); print_ast_json(f->cond, indent + 1); }
    if (f->update) { printf(",\"update\":"); print_ast_json(f->update, indent + 1); }
    printf(",\"body\":");
    print_ast_json(f->body, indent + 1);
    break;
  }
  case AST_RETURN: {
    const ast_return_t *ret = (const ast_return_t *)node;
    if (ret->value) { printf(",\"value\":"); print_ast_json(ret->value, indent + 1); }
    break;
  }
  case AST_ASSIGN: {
    const ast_assign_t *asgn = (const ast_assign_t *)node;
    printf(",\"name\":");
    print_json_string(asgn->name.ptr, asgn->name.len);
    if (asgn->op) {
      size_t tlen = 0;
      const char *ttext = token_get_text(asgn->op, &tlen);
      printf(",\"op\":");
      print_json_string(ttext, tlen);
    }
    printf(",\"value\":");
    print_ast_json(asgn->value, indent + 1);
    break;
  }
  case AST_EXPR_STMT: {
    const ast_expr_stmt_t *es = (const ast_expr_stmt_t *)node;
    printf(",\"expr\":");
    print_ast_json(es->expr, indent + 1);
    break;
  }
  case AST_DISCARD: {
    const ast_discard_t *d = (const ast_discard_t *)node;
    printf(",\"expr\":");
    print_ast_json(d->expr, indent + 1);
    break;
  }
  case AST_BINARY: {
    const ast_binary_t *bin = (const ast_binary_t *)node;
    if (bin->op) {
      size_t tlen = 0;
      const char *ttext = token_get_text(bin->op, &tlen);
      printf(",\"op\":");
      print_json_string(ttext, tlen);
    }
    printf(",\"lhs\":");
    print_ast_json(bin->lhs, indent + 1);
    printf(",\"rhs\":");
    print_ast_json(bin->rhs, indent + 1);
    break;
  }
  case AST_UNARY: {
    const ast_unary_t *un = (const ast_unary_t *)node;
    if (un->op) {
      size_t tlen = 0;
      const char *ttext = token_get_text(un->op, &tlen);
      printf(",\"op\":");
      print_json_string(ttext, tlen);
    }
    printf(",\"operand\":");
    print_ast_json(un->operand, indent + 1);
    break;
  }
  case AST_CALL: {
    const ast_call_t *call = (const ast_call_t *)node;
    printf(",\"callee\":");
    print_ast_json(call->callee, indent + 1);
    printf(",\"args\":");
    print_json_siblings(call->args, indent + 1);
    break;
  }
  case AST_MEMBER: {
    const ast_member_t *m = (const ast_member_t *)node;
    printf(",\"object\":");
    print_ast_json(m->object, indent + 1);
    printf(",\"field\":");
    print_json_string(m->field.ptr, m->field.len);
    break;
  }
  case AST_INDEX: {
    const ast_index_t *idx = (const ast_index_t *)node;
    printf(",\"object\":");
    print_ast_json(idx->object, indent + 1);
    printf(",\"indices\":");
    print_json_siblings(idx->indices, indent + 1);
    break;
  }
  case AST_CAST: {
    const ast_cast_t *cast = (const ast_cast_t *)node;
    printf(",\"expr\":");
    print_ast_json(cast->expr, indent + 1);
    if (cast->target_type.len > 0) {
      printf(",\"type\":");
      print_json_string(cast->target_type.ptr, cast->target_type.len);
    }
    break;
  }
  case AST_IDENT: {
    const ast_ident_t *id = (const ast_ident_t *)node;
    printf(",\"name\":");
    print_json_string(id->name.ptr, id->name.len);
    break;
  }
  case AST_INT_LIT: {
    const ast_int_lit_t *lit = (const ast_int_lit_t *)node;
    printf(",\"value\":\"%" PRId64 "\"", lit->value);
    break;
  }
  case AST_FLOAT_LIT: {
    const ast_float_lit_t *lit = (const ast_float_lit_t *)node;
    printf(",\"value\":\"%.17g\"", lit->value);
    break;
  }
  case AST_BOOL_LIT: {
    const ast_bool_lit_t *lit = (const ast_bool_lit_t *)node;
    printf(",\"value\":%s", lit->value ? "true" : "false");
    break;
  }
  case AST_STRING_LIT: {
    const ast_string_lit_t *lit = (const ast_string_lit_t *)node;
    printf(",\"value\":");
    print_json_string(lit->text.ptr, lit->text.len);
    break;
  }
  case AST_CHAR_LIT: {
    const ast_char_lit_t *lit = (const ast_char_lit_t *)node;
    printf(",\"value\":%u", (unsigned)lit->value);
    break;
  }
  case AST_ERROR: {
    const ast_error_t *err = (const ast_error_t *)node;
    printf(",\"message\":");
    print_json_string(err->message.ptr, err->message.len);
    break;
  }
  case AST_BREAK:
  case AST_CONTINUE:
    /* 无额外字段 */
    break;
  default:
    break;
  }

  putchar('}');
}

/** 兄弟链输出为 JSON 数组 */
static void print_json_siblings(const ast_node_t *head, int indent) {
  putchar('[');
  if (head) {
    print_ast_json(head, indent);
    for (const ast_node_t *s = head->next; s; s = s->next) {
      putchar(',');
      print_ast_json(s, indent);
    }
  }
  putchar(']');
}

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

  /* Stage ④: 输出 AST JSON（lexer 仍存活，strslice 可安全访问） */
  print_ast_json(ast, 0);
  putchar('\n');

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

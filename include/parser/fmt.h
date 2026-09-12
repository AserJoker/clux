#ifndef _H_CLUX_PARSER_FMT_
#define _H_CLUX_PARSER_FMT_

#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"
#include "core/vec.h"

/**
 * clux 源码格式化（最小版）。
 *
 * 策略：**基于 token 流规整 trivia**，不重排代码结构。输入是 lexer 产出的
 * 完整 token 池（含 WHITESPACE / COMMENT / MULTILINE_COMMENT），输出是
 * 重新排版后的源码文本。因为注释与空白本身就是 token，天然被保留。
 *
 * 格式规则：
 *   - 缩进一律 4 空格（源中的 TAB 被替换）
 *   - `{` 跟随前行（K&R），`{` 后换行；`}` 单独一行
 *   - 空块 `{}` 紧凑写在同一行
 *   - `;` 后换行（`for` 头部括号内的 `;` 除外，改为后跟空格）
 *   - `}` 后跟 `else` 时写在同一行（`} else {`）
 *
 * 幂等性：格式化结果再格式化应保持不变（由测试保证）。
 *
 * @param alloc    输出缓冲的分配器
 * @param tokens   lexer 产出的 token 池（vec<token_t*>，含 trivia）
 * @param out_len  可选，取回结果长度（不含结尾 NUL）
 * @return 格式化后的 NUL 结尾字符串（alloc 分配，调用方 allocator_free）；
 *         失败返回 NULL
 */
char *fmt_format(allocator_t *alloc, const vec_t *tokens, size_t *out_len);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_PARSER_FMT_ */

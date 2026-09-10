#include "vm/exec.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "vm/type.h"
#include "vm/type_type.h"
#include "vm/type_interrupt.h"
#include "vm/bcode_function.h"
#include "core/panic.h"
#include "core/string.h"
#include "core/vec.h"

#include <string.h>

/* ================================================================ */
/* 操作数栈（借用引用，归 scope 管理生命周期）                         */
/* ================================================================ */

void exec_stack_push(vm_t *vm, value_t *v) {
    vec_push(vm->stack, vm->alloc, v);
}

value_t *exec_stack_pop(vm_t *vm) {
    return (value_t *)vec_pop(vm->stack);
}

value_t *exec_stack_peek(const vm_t *vm, size_t offset) {
    size_t sp = vec_len(vm->stack);
    if (offset >= sp) panic("exec: stack underflow (sp=%zu, offset=%zu)", sp, offset);
    return (value_t *)vec_get(vm->stack, sp - 1 - offset);
}

/* ================================================================ */
/* 指令回调                                                          */
/* ================================================================ */

/* ---- 变量访问 ---- */

static value_t *op_push(vm_t *vm, bytecode_t *bc, size_t *pc) {
    strslice_t name = bcode_read_str(bc, pc);
    value_t *v = scope_lookup(vm->current_scope, name);
    if (!v) return value_make_error(vm, "exec: undefined variable");
    /* TDZ 检查由 value_xxx 消费层统一处理（右值被消费时返回 error） */
    return v; /* 借用引用 */
}

static value_t *op_store(vm_t *vm, bytecode_t *bc, size_t *pc) {
    strslice_t name = bcode_read_str(bc, pc);
    value_t *src = exec_stack_pop(vm);
    value_t *dst = scope_lookup(vm->current_scope, name);
    if (!dst) return value_make_error(vm, "exec: undefined variable in assignment");
    /* TDZ 退出由 value_assign 统一处理（赋值成功清 dst TDZ） */
    return value_assign(vm, dst, src);
}

/* ---- 字面量 ---- */

static value_t *op_push_i8(vm_t *vm, bytecode_t *bc, size_t *pc) {
    int8_t v = bcode_read_i8(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_i8, &v);
    return value_make(vm, vm->type_i8, data);
}
static value_t *op_push_i16(vm_t *vm, bytecode_t *bc, size_t *pc) {
    int16_t v = bcode_read_i16(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_i16, &v);
    return value_make(vm, vm->type_i16, data);
}
static value_t *op_push_i32(vm_t *vm, bytecode_t *bc, size_t *pc) {
    int32_t v = bcode_read_i32(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_i32, &v);
    return value_make(vm, vm->type_i32, data);
}
static value_t *op_push_i64(vm_t *vm, bytecode_t *bc, size_t *pc) {
    int64_t v = bcode_read_i64(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_i64, &v);
    return value_make(vm, vm->type_i64, data);
}
static value_t *op_push_u8(vm_t *vm, bytecode_t *bc, size_t *pc) {
    uint8_t v = bcode_read_u8(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_u8, &v);
    return value_make(vm, vm->type_u8, data);
}
static value_t *op_push_u16(vm_t *vm, bytecode_t *bc, size_t *pc) {
    uint16_t v = bcode_read_u16(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_u16, &v);
    return value_make(vm, vm->type_u16, data);
}
static value_t *op_push_u32(vm_t *vm, bytecode_t *bc, size_t *pc) {
    uint32_t v = bcode_read_u32(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_u32, &v);
    return value_make(vm, vm->type_u32, data);
}
static value_t *op_push_u64(vm_t *vm, bytecode_t *bc, size_t *pc) {
    uint64_t v = bcode_read_u64(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_u64, &v);
    return value_make(vm, vm->type_u64, data);
}
static value_t *op_push_f32(vm_t *vm, bytecode_t *bc, size_t *pc) {
    float v = bcode_read_f32(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_f32, &v);
    return value_make(vm, vm->type_f32, data);
}
static value_t *op_push_f64(vm_t *vm, bytecode_t *bc, size_t *pc) {
    double v = bcode_read_f64(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_f64, &v);
    return value_make(vm, vm->type_f64, data);
}
static value_t *op_push_bool(vm_t *vm, bytecode_t *bc, size_t *pc) {
    bool v = bcode_read_bool(bc, pc);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &v);
    return value_make(vm, vm->type_bool, data);
}
static value_t *op_push_str(vm_t *vm, bytecode_t *bc, size_t *pc) {
    strslice_t s = bcode_read_str(bc, pc);
    string_t *str = string_from_bytes(vm->alloc, s.ptr, s.len);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_str, &str);
    return value_make(vm, vm->type_str, data);
}

/* ---- 栈操作 ---- */

static value_t *op_push_value(vm_t *vm, bytecode_t *bc, size_t *pc) {
    uint32_t offset = bcode_read_u32(bc, pc);
    return exec_stack_peek(vm, offset); /* offset=0 即 dup 栈顶 */
}

static value_t *op_pop(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)bc; (void)pc;
    exec_stack_pop(vm); /* 丢弃借用引用，不释放 */
    return NULL;
}

/* ---- 类型与变量定义 ---- */

static value_t *op_load(vm_t *vm, bytecode_t *bc, size_t *pc) {
    strslice_t name = bcode_read_str(bc, pc);
    value_t *v = scope_lookup(vm->global_scope, name);
    if (!v) return value_make_error(vm, "exec: undefined type");
    return v; /* type value 借用引用 */
}

static value_t *op_push_undefined(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)bc; (void)pc;
    return value_make_undefined(vm);
}

static value_t *op_define(vm_t *vm, bytecode_t *bc, size_t *pc) {
    strslice_t name = bcode_read_str(bc, pc);

    /* 栈顶判定：type value（LOAD 压入）或 void/undefined（PUSH_UNDEFINED 压入）
       作为类型说明符 → 再弹一个值；否则栈顶即值本身（函数参数定义场景） */
    value_t *top = exec_stack_peek(vm, 0);
    bool is_type_spec = (value_type(top) == vm->type_type) || value_is_undefined(vm, top);

    const type_t *decl_type = NULL;
    value_t *init;
    if (is_type_spec) {
        value_t *spec = exec_stack_pop(vm); /* 类型说明符 */
        init = exec_stack_pop(vm);
        if (value_type(spec) == vm->type_type) {
            decl_type = value_as(spec, const type_t *);
        }
        /* spec 是 undefined：无显式类型，从值推断 */
    } else {
        init = exec_stack_pop(vm);
    }
    if (!decl_type) decl_type = value_type(init);

    /* 无初始值（init 为 void/undefined）：以声明类型构造 TDZ 变量 */
    if (value_is_undefined(vm, init)) {
        void *data = value_alloc_data(vm->alloc, decl_type);
        value_t *tdz = value_make(vm, decl_type, data);
        value_set_tdz(tdz, true);
        value_t *stored = scope_define(vm, vm->current_scope, name.ptr, tdz);
        if (value_is_error(vm, stored)) return stored;
        /* scope_define clone 不传播 is_tdz（vtable clone 走 value_make），直接设置存储值 */
        value_set_tdz(stored, true);
        return NULL;
    }

    value_t *stored = scope_define(vm, vm->current_scope, name.ptr, init);
    if (value_is_error(vm, stored)) return stored;
    return NULL;
}

/* ---- 二元/一元运算 ---- */

#define BINARY_OP(name, value_fn)                                              \
    static value_t *op_##name(vm_t *vm, bytecode_t *bc, size_t *pc) {         \
        (void)bc; (void)pc;                                                    \
        value_t *b = exec_stack_pop(vm);                                       \
        value_t *a = exec_stack_pop(vm);                                       \
        return value_fn(vm, a, b);                                             \
    }

BINARY_OP(add, value_add)   BINARY_OP(sub, value_sub)   BINARY_OP(mul, value_mul)
BINARY_OP(div, value_div)   BINARY_OP(mod, value_mod)
BINARY_OP(eq, value_eq)     BINARY_OP(ne, value_ne)     BINARY_OP(lt, value_lt)
BINARY_OP(le, value_le)     BINARY_OP(gt, value_gt)     BINARY_OP(ge, value_ge)
BINARY_OP(band, value_band) BINARY_OP(bor, value_bor)   BINARY_OP(bxor, value_bxor)
BINARY_OP(shl, value_shl)   BINARY_OP(shr, value_shr)

static value_t *op_neg(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)bc; (void)pc;
    return value_neg(vm, exec_stack_pop(vm));
}
static value_t *op_not(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)bc; (void)pc;
    return value_lnot(vm, exec_stack_pop(vm));
}
static value_t *op_bnot(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)bc; (void)pc;
    return value_bnot(vm, exec_stack_pop(vm));
}

/* ---- 显式转换 ---- */

static value_t *op_cast(vm_t *vm, bytecode_t *bc, size_t *pc) {
    /* 类型经栈顶 type value 传递（LOAD 压入），无立即数操作数：
       弹 type value → 弹被转换值 → value_explicit_cast（error/TDZ 内部短路） */
    (void)bc; (void)pc;
    value_t *vtype = exec_stack_pop(vm);
    value_t *value = exec_stack_pop(vm);
    const type_t *target = *(const type_t **)value_data(vtype);
    return value_explicit_cast(vm, value, target);
}

/* ---- 函数 ---- */

static value_t *op_create_func_type(vm_t *vm, bytecode_t *bc, size_t *pc) {
    /* 弹栈格式 [return, param1..argc, is_variadic]（压栈序，docs 2.7.6）：
       栈顶 is_variadic（PUSH_BOOL false）→ 倒序取参数 → 返回值类型 */
    uint32_t argc = bcode_read_u32(bc, pc);
    value_t *variadic_v = exec_stack_pop(vm);
    bool is_variadic = *(const bool *)value_data(variadic_v);
    const type_t *params[argc > 0 ? argc : 1];
    for (uint32_t i = argc; i-- > 0; ) {
        params[i] = *(const type_t **)value_data(exec_stack_pop(vm));
    }
    const type_t *ret = *(const type_t **)value_data(exec_stack_pop(vm));
    /* intern 签名类型（按签名去重，同签名共享一个 type_t） */
    const type_t *sig = type_func_sig(vm, params, argc, ret, is_variadic);
    return type_as_value(vm, sig);
}

static value_t *op_push_function(vm_t *vm, bytecode_t *bc, size_t *pc) {
    /* entry_pc 立即数；弹栈顶签名类型（CREATE_FUNC_TYPE 产物），
       构造 bcode_function_t（封装在 bcode_function 模块内） */
    uint32_t entry_pc = bcode_read_u32(bc, pc);
    value_t *sig_v = exec_stack_pop(vm);
    const type_t *sig = *(const type_t **)value_data(sig_v);
    return bcode_function_new(vm, sig, entry_pc, vm->root_scope);
}

static value_t *op_define_function(vm_t *vm, bytecode_t *bc, size_t *pc) {
    /* 单值弹栈（函数值自带签名类型，无类型说明符；与 DEFINE 双弹区分）。
       函数值是 auto-track 的普通一等值：scope_define clone 进 scope 后，
       原值壳归 scope 统一回收（与 op_define 一致，不手动释放，避免 double
       free）；func_t 本体归 vm->functions 池 */
    strslice_t name = bcode_read_str(bc, pc);
    value_t *fnv = exec_stack_pop(vm);
    value_t *stored = scope_define(vm, vm->current_scope, name.ptr, fnv);
    if (value_is_error(vm, stored)) return stored;
    return NULL;
}

static value_t *op_call(vm_t *vm, bytecode_t *bc, size_t *pc) {
    /* callee 在 stack[sp-1-argc]，args 为紧随其后的 argc 个引用。
       args 复制进 VLA（value_call 内部压栈可能 realloc 使栈缓冲悬垂）；
       借用引用，值本体归 scope。 */
    uint32_t argc = bcode_read_u32(bc, pc);
    size_t sp = vec_len(vm->stack);
    value_t *callee = exec_stack_peek(vm, argc);
    value_t *args[argc > 0 ? argc : 1];
    for (uint32_t i = 0; i < argc; i++) {
        args[i] = (value_t *)vec_get(vm->stack, sp - argc + i);
    }
    for (uint32_t i = 0; i <= argc; i++) exec_stack_pop(vm);
    return value_call(vm, callee, args, argc);
}

static value_t *op_ret(vm_t *vm, bytecode_t *bc, size_t *pc) {
    /* 栈顶即返回值；返回 interrupt 哨兵由 bcode_call_cfunc 子循环捕获 */
    (void)bc; (void)pc;
    return value_make_interrupt(vm, INTERRUPT_RETURN);
}

/* ---- 控制流 ---- */

static value_t *op_jmp(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)vm;
    uint32_t target = bcode_read_u32(bc, pc);
    *pc = target;
    return NULL;
}

static value_t *op_jz(vm_t *vm, bytecode_t *bc, size_t *pc) {
    uint32_t target = bcode_read_u32(bc, pc);
    value_t *v = exec_stack_pop(vm);
    if (value_is_error(vm, v)) return v;
    /* 严格 bool：safe_cast 到 bool（TDZ/不可转换 → error 传播） */
    value_t *cond = value_explicit_cast(vm, v, vm->type_bool);
    if (value_is_error(vm, cond)) return cond;
    if (!*(const bool *)value_data(cond)) *pc = target;
    return NULL;
}

static value_t *op_jnz(vm_t *vm, bytecode_t *bc, size_t *pc) {
    uint32_t target = bcode_read_u32(bc, pc);
    value_t *v = exec_stack_pop(vm);
    if (value_is_error(vm, v)) return v;
    /* 严格 bool：safe_cast 到 bool（TDZ/不可转换 → error 传播） */
    value_t *cond = value_explicit_cast(vm, v, vm->type_bool);
    if (value_is_error(vm, cond)) return cond;
    if (*(const bool *)value_data(cond)) *pc = target;
    return NULL;
}

/* ---- 作用域 ---- */

static value_t *op_push_scope(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)bc; (void)pc;
    vm_push_scope(vm);
    return NULL;
}

static value_t *op_pop_scope(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)bc; (void)pc;
    vm_pop_scope(vm);
    return NULL;
}

/* ---- 终止 ---- */

static value_t *op_halt(vm_t *vm, bytecode_t *bc, size_t *pc) {
    (void)bc; (void)pc;
    vm->halted = true;
    return NULL;
}

/* ================================================================ */
/* 指令分派表（opcode → 回调）                                         */
/* ================================================================ */

static const bcode_handler_t HANDLERS[] = {
    [BCODE_PUSH]           = op_push,
    [BCODE_STORE]          = op_store,
    [BCODE_PUSH_STR]       = op_push_str,
    [BCODE_PUSH_I8]        = op_push_i8,
    [BCODE_PUSH_I16]       = op_push_i16,
    [BCODE_PUSH_I32]       = op_push_i32,
    [BCODE_PUSH_I64]       = op_push_i64,
    [BCODE_PUSH_U8]        = op_push_u8,
    [BCODE_PUSH_U16]       = op_push_u16,
    [BCODE_PUSH_U32]       = op_push_u32,
    [BCODE_PUSH_U64]       = op_push_u64,
    [BCODE_PUSH_F32]       = op_push_f32,
    [BCODE_PUSH_F64]       = op_push_f64,
    [BCODE_PUSH_BOOL]      = op_push_bool,
    [BCODE_PUSH_VALUE]     = op_push_value,
    [BCODE_LOAD]           = op_load,
    [BCODE_PUSH_UNDEFINED] = op_push_undefined,
    [BCODE_DEFINE]         = op_define,
    [BCODE_ADD]            = op_add,
    [BCODE_SUB]            = op_sub,
    [BCODE_MUL]            = op_mul,
    [BCODE_DIV]            = op_div,
    [BCODE_MOD]            = op_mod,
    [BCODE_EQ]             = op_eq,
    [BCODE_NE]             = op_ne,
    [BCODE_LT]             = op_lt,
    [BCODE_LE]             = op_le,
    [BCODE_GT]             = op_gt,
    [BCODE_GE]             = op_ge,
    [BCODE_AND]            = op_band,
    [BCODE_OR]             = op_bor,
    [BCODE_BXOR]           = op_bxor,
    [BCODE_SHL]            = op_shl,
    [BCODE_SHR]            = op_shr,
    [BCODE_NEG]            = op_neg,
    [BCODE_NOT]            = op_not,
    [BCODE_BNOT]           = op_bnot,
    [BCODE_CAST]           = op_cast,
    [BCODE_CREATE_FUNC_TYPE] = op_create_func_type,
    [BCODE_PUSH_FUNCTION]  = op_push_function,
    [BCODE_DEFINE_FUNCTION] = op_define_function,
    [BCODE_CALL]           = op_call,
    [BCODE_RET]            = op_ret,
    [BCODE_JMP]            = op_jmp,
    [BCODE_JZ]             = op_jz,
    [BCODE_JNZ]            = op_jnz,
    [BCODE_PUSH_SCOPE]     = op_push_scope,
    [BCODE_POP_SCOPE]      = op_pop_scope,
    [BCODE_POP]            = op_pop,
    [BCODE_HALT]           = op_halt,
};

/* ================================================================ */
/* 驱动循环 + 主循环                                                  */
/* ================================================================ */

value_t *exec_drive(vm_t *vm, bytecode_t *bc, size_t start_pc) {
    vm->pc = start_pc;
    for (;;) {
        if (vm->halted) return NULL;  /* HALT 指令设 halted，正常退出 */
        bcode_op_t op = bcode_read_op(bc, &vm->pc);
        bcode_handler_t h = HANDLERS[op];
        if (!h) return value_make_error(vm, "exec: unimplemented opcode");
        value_t *r = h(vm, bc, &vm->pc);
        if (r) exec_stack_push(vm, r);
        if (value_is_error(vm, r)) return r;      /* 引擎级硬错误，停止 */
        if (value_is_interrupt(vm, r)) return r;  /* RET 哨兵，函数子循环捕获 */
    }
}

value_t *exec_run(vm_t *vm, bytecode_t *bc) {
    vm->bc     = bc;
    vm->halted = false;

    /* 清空操作数栈（借用引用，不释放） */
    while (!vec_is_empty(vm->stack)) vec_pop(vm->stack);

    /* clux 无顶层语句：只执行函数注册段，入口函数由调用方
       scope_lookup + value_call 显式触发 */
    return exec_drive(vm, bc, 0);
}

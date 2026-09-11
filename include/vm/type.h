#ifndef _H_CLUX_VM_TYPE_
#define _H_CLUX_VM_TYPE_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/vtable.h"
#include "core/strslice.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * type_kind_t: 类型粗粒度分类（type_t.kind）
 *
 * 鸭子类型判断的基础：复合/修饰类型按 kind 而非实例指针分类。
 * 粗粒度（INT/FLOAT 覆盖全部宽度变体），细粒度差异由 name/size 承载。
 * M2 预留 STRUCT/ARRAY/TUPLE/ENUM/CUNION。
 */
typedef enum type_kind_t {
    TYPE_KIND_VOID     = 0,
    TYPE_KIND_BOOL,
    TYPE_KIND_INT,       /* i8/i16/i32/i64/u8/u16/u32/u64 */
    TYPE_KIND_FLOAT,     /* f32/f64 */
    TYPE_KIND_STR,
    TYPE_KIND_TYPE,      /* type value 的元类型 */
    TYPE_KIND_FUNC,
    TYPE_KIND_ERROR,
    TYPE_KIND_INTERRUPT, /* 引擎级控制流哨兵（interrupt 类型） */
    TYPE_KIND_CONST,     /* const 修饰（持 sub） */
    TYPE_KIND_VOLATILE,  /* volatile 修饰（持 sub） */
    /* ---- M2 预留：复合类型 ---- */
    TYPE_KIND_STRUCT,
    TYPE_KIND_ARRAY,
    TYPE_KIND_TUPLE,
    TYPE_KIND_ENUM,
    TYPE_KIND_CUNION,
} type_kind_t;

/**
 * func_sig_t: 函数签名（参数 + 返回值信息）
 *
 * 内联在 func_type_t 中。同一签名的所有函数共享同一个签名类型
 * func_type_t（由 vm 类型池按签名去重 intern），因此 func value 的
 * type 即携带完整签名，shadow 值（data=NULL）也能完成调用类型检查。
 */
typedef struct func_sig_t {
    const type_t **params;      /* array[param_count]; 元素可为 NULL = 无类型约束 */
    size_t         param_count;
    const type_t  *return_type; /* NULL = void */
    bool           is_variadic;
} func_sig_t;

/**
 * type_t: 类型描述符（基类）
 *
 * 每种类型对应一个 type_t 单例，由 vm 统一管理。
 * 类型本身也是一种 value（通过 type_as_value 获取）。
 *
 * 需要附加信息的类型通过 C 继承扩展：子结构体首成员为 type_t base，
 * 向上转型 (type_t *) 使用，向下转型 (func_type_t *) 读取扩展字段。
 */
typedef struct type_t {
    const vtable_t *vtable;
    strslice_t      name;       /* 类型名，如 "i32", "f64", "str" */
    size_t          size;       /* 该类型数据的字节大小 */
    size_t          align;      /* 该类型数据的对齐要求 */
    type_kind_t     kind;       /* 粗粒度分类（鸭子类型判断用） */
} type_t;

/**
 * func_type_t: 函数签名类型（type_t 的扩展）
 *
 * 首成员 base 必须为 type_t（向上转型安全）。sig 携带参数/返回值信息，
 * 由 vm 类型池 intern（type_func_sig），vm 拥有生命周期。vm->type_func
 * 是无签名的 func 基类，也是 func_type_t 布局（sig 为空），向下转型安全。
 */
typedef struct func_type_t {
    type_t      base;
    func_sig_t  sig;
} func_type_t;

/**
 * const_type_t / volatile_type_t: 前导修饰类型（type_t 的扩展）
 *
 * 真实类型（const i32 != i32），持 sub 指针指向被修饰的底层类型。
 * 由 vm 类型池 intern（type_const_intern / type_volatile_intern），vm
 * 拥有生命周期。运算行为（vtable）代理到 sub（解包语义，见 m2-design §10）：
 * volatile(const(i32)) 组合时 volatile 外层、const 内层（固定顺序）。
 */
typedef struct const_type_t {
    type_t      base;
    const type_t *sub;
} const_type_t;

typedef struct volatile_type_t {
    type_t      base;
    const type_t *sub;
} volatile_type_t;

/**
 * 按名从当前作用域链解析类型（类型即表达式）
 *
 * 类型名与变量同机制：内建类型值注册在 global scope（vm_register_builtin_types），
 * 自定义类型（M2 type 定义）注册到定义点当前作用域。scope_lookup 沿
 * current_scope → root_scope → global_scope 链查找，变量遮蔽类型天然成立
 * （命中非 type value 时返回 NULL）。
 */
const type_t *type_lookup(const vm_t *vm, strslice_t name);

/** 判断两个类型是否相同（指针比较，因为类型是单例） */
static inline bool type_eq(const type_t *a, const type_t *b) {
    return a == b;
}

/**
 * 鸭子类型相等判断：分派 a->vtable->type_equal(vm, a, b)。
 * NULL 槽位 = 默认指针比较（基础类型单例）。
 * 复合类型（M2 struct/array/tuple）按成员结构递归判断。
 */
bool type_equal(vm_t *vm, const type_t *a, const type_t *b);

/**
 * 类型兼容性判断（T extends U）：sub 是否可兼容 sup（鸭子类型转换）。
 * 分派 sub->vtable->type_extends(vm, sub, sup)；NULL 槽位 = 默认同类型
 * （type_equal）。M2 复合类型按成员递归判断。
 */
bool type_extends(vm_t *vm, const type_t *sub, const type_t *sup);

/** 判断类型是否为 const 修饰类型（type_kind 分类） */
static inline bool type_is_const(const type_t *t) {
    return t && t->kind == TYPE_KIND_CONST;
}

/** 判断类型是否为 volatile 修饰类型（type_kind 分类） */
static inline bool type_is_volatile(const type_t *t) {
    return t && t->kind == TYPE_KIND_VOLATILE;
}

/** 取修饰类型的 sub（非 const/volatile 时返回 NULL） */
const type_t *type_qualifier_sub(const type_t *t);

/**
 * 类型是否含 const 修饰（递归 sub 链）：const i32 → true；
 * volatile(const(i32)) → true；volatile(i32) → false。
 * sema 赋值左值检查用。
 */
bool type_has_const(const type_t *t);

/** const 类型 intern（按 sub 指针去重，vm 拥有生命周期） */
const type_t *type_const_intern(vm_t *vm, const type_t *sub);

/** volatile 类型 intern（按 sub 指针去重，vm 拥有生命周期） */
const type_t *type_volatile_intern(vm_t *vm, const type_t *sub);

/** 将 type 转为 value_t*（type 作为 first-class value） */
value_t *type_as_value(vm_t *vm, const type_t *t);

/**
 * 类型提升（二元运算前协商结果类型）
 *
 * 优先级：f64 > f32 > u64 > i64 > u32 > i32 > u16 > i16 > u8 > i8 > bool
 * - 同类型直接返回
 * - 两个数值类型返回高 rank 的那个
 * - 非数值类型（str/void/type/func/error）或类型不兼容返回 NULL
 */
const type_t *type_promote(const vm_t *vm, const type_t *a, const type_t *b);

/**
 * 函数签名类型 intern（按签名去重）
 *
 * 以 params（可为 NULL 或含 NULL 元素 = 无类型约束）、param_count、
 * return_type、is_variadic 标识一个函数签名。相同签名返回同一个
 * type_t（vm 类型池持有，vm_destroy 时释放）。params 数组会被复制，
 * 调用方的临时数组可自行释放。
 */
const type_t *type_func_sig(vm_t *vm, const type_t *const *params,
                            size_t param_count, const type_t *return_type,
                            bool is_variadic);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_TYPE_ */

#include "vm/type_float.h"
#include "vm/value.h"
#include "vm/vm.h"
#include "core/panic.h"

#include <math.h>

/* ---- 按类型宽度读写 ---- */

static double float_read(const value_t *v) {
    if (value_type(v)->size == sizeof(float))
        return (double)*(const float *)value_data(v);
    return *(const double *)value_data(v);
}

static value_t *float_store(vm_t *vm, const type_t *type, double val) {
    void *data = value_alloc_data(vm->alloc, type);
    if (type->size == sizeof(float))
        *(float *)data = (float)val;
    else
        *(double *)data = val;
    return value_make(vm, type, data);
}

static value_t *bool_store(vm_t *vm, bool val) {
    void *data = value_alloc_data(vm->alloc, vm->type_bool);
    *(bool *)data = val;
    return value_make(vm, vm->type_bool, data);
}

/* ---- 算术运算 ---- */

static value_t *float_add(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, add, "+");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "+: type mismatch");
    return float_store(vm, value_type(a), float_read(a) + float_read(b));
}

static value_t *float_sub(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, sub, "-");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "-: type mismatch");
    return float_store(vm, value_type(a), float_read(a) - float_read(b));
}

static value_t *float_mul(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, mul, "*");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "*: type mismatch");
    return float_store(vm, value_type(a), float_read(a) * float_read(b));
}

static value_t *float_div(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, div, "/");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "/: type mismatch");
    double bv = float_read(b);
    if (bv == 0.0) panic("division by zero");
    return float_store(vm, value_type(a), float_read(a) / bv);
}

static value_t *float_mod(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, mod, "%");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "%: type mismatch");
    return float_store(vm, value_type(a), fmod(float_read(a), float_read(b)));
}

static value_t *float_neg(vm_t *vm, value_t *a) {
    if (value_is_error(vm, a)) return a;
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "-: type mismatch");
    return float_store(vm, value_type(a), -float_read(a));
}

/* ---- 比较运算 ---- */

static value_t *float_eq(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, eq, "==");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "==: type mismatch");
    return bool_store(vm, float_read(a) == float_read(b));
}

static value_t *float_ne(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, ne, "!=");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "!=: type mismatch");
    return bool_store(vm, float_read(a) != float_read(b));
}

static value_t *float_lt(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, lt, "<");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "<: type mismatch");
    return bool_store(vm, float_read(a) < float_read(b));
}

static value_t *float_le(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, le, "<=");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, "<=: type mismatch");
    return bool_store(vm, float_read(a) <= float_read(b));
}

static value_t *float_gt(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, gt, ">");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, ">: type mismatch");
    return bool_store(vm, float_read(a) > float_read(b));
}

static value_t *float_ge(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, ge, ">=");
    if (value_type(a) != vm->type_f32 && value_type(a) != vm->type_f64)
        return value_make_error(vm, ">=: type mismatch");
    return bool_store(vm, float_read(a) >= float_read(b));
}

/* ---- 隐式转换：f32 → f64 ---- */

static value_t *float_implicit_cast(vm_t *vm, value_t *v, const type_t *target) {
    if (target != vm->type_f64)
        return value_make_error(vm, "implicit cast: float can only widen to f64");

    return float_store(vm, target, float_read(v));
}

/* ---- 显式转换 ---- */

static value_t *float_explicit_cast(vm_t *vm, value_t *v, const type_t *target) {
    double dv = float_read(v);

    if (target == vm->type_f32 || target == vm->type_f64) {
        return float_store(vm, target, dv);
    }

    /* float → int：向零截断，按目标宽度存储 */
    if (target == vm->type_i8 || target == vm->type_i16 ||
        target == vm->type_i32 || target == vm->type_i64 ||
        target == vm->type_u8 || target == vm->type_u16 ||
        target == vm->type_u32 || target == vm->type_u64) {
        void *data = value_alloc_data(vm->alloc, target);
        switch (target->size) {
            case 1: *(int8_t  *)data = (int8_t)dv;  break;
            case 2: *(int16_t *)data = (int16_t)dv; break;
            case 4: *(int32_t *)data = (int32_t)dv; break;
            default: *(int64_t *)data = (int64_t)dv; break;
        }
        return value_make(vm, target, data);
    }

    if (target == vm->type_bool) {
        return bool_store(vm, dv != 0.0);
    }

    return value_make_error(vm, "explicit cast: incompatible target type");
}

const vtable_t VTABLE_FLOAT = {
    .add = float_add,  .sub = float_sub,
    .mul = float_mul,  .div = float_div,
    .mod = float_mod,  .neg = float_neg,
    .eq  = float_eq,   .ne  = float_ne,
    .lt  = float_lt,   .le  = float_le,
    .gt  = float_gt,   .ge  = float_ge,
    .implicit_cast = float_implicit_cast,
    .explicit_cast = float_explicit_cast,
};

#include "vm/bcode.h"
#include "vm/vm.h"
#include "core/panic.h"
#include "core/string.h"
#include "core/vec.h"

#include <string.h>

/* ---- 字节缓冲（code 流）：增长 + 原地覆写 ---- */

static void buf_append(bytecode_t *bc, const uint8_t *data, size_t n) {
    if (bc->code.len + n > bc->code.cap) {
        size_t new_cap = bc->code.cap ? bc->code.cap : 64;
        while (new_cap < bc->code.len + n) new_cap *= 2;
        uint8_t *nd = (uint8_t *)allocator_new_ex(
            bc->alloc, "clux.vm.bcode.buf", new_cap,
            NULL, NULL, NULL, 1);
        if (bc->code.data) {
            memcpy(nd, bc->code.data, bc->code.len);
            allocator_free(bc->alloc, (void **)&bc->code.data);
        }
        bc->code.data = nd;
        bc->code.cap = new_cap;
    }
    memcpy(bc->code.data + bc->code.len, data, n);
    bc->code.len += n;
}

/* ---- 字节序工具：小端读写（跨平台可落盘一致） ---- */

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- 生命周期 ---- */

bytecode_t *bcode_new(allocator_t *alloc) {
    bytecode_t *bc = (bytecode_t *)allocator_new_ex(
        alloc, "clux.vm.bcode", sizeof(bytecode_t),
        NULL, NULL, NULL, 1);
    bc->alloc = alloc;
    bc->strs  = vec_new(alloc, /*owns_element=*/true);
    bc->code.data = NULL;
    bc->code.len  = 0;
    bc->code.cap  = 0;
    return bc;
}

void bcode_destroy(bytecode_t **bc) {
    if (!bc || !*bc) return;
    bytecode_t *b = *bc;
    vec_free(b->alloc, &b->strs);
    if (b->code.data) allocator_free(b->alloc, (void **)&b->code.data);
    allocator_free(b->alloc, (void **)bc);
}

/* ---- strtable 访问 ---- */

size_t bcode_str_count(const bytecode_t *bc) {
    return vec_len(bc->strs);
}

strslice_t bcode_str_at(const bytecode_t *bc, size_t idx) {
    string_t *s = (string_t *)vec_get(bc->strs, idx);
    if (!s) return STRSLICE_EMPTY;
    return strslice_from_cstr(string_cstr(s));
}

size_t bcode_str_index(bytecode_t *bc, strslice_t s) {
    for (size_t i = 0; i < vec_len(bc->strs); i++) {
        string_t *st = (string_t *)vec_get(bc->strs, i);
        if (strslice_eq(strslice_from_cstr(string_cstr(st)), s)) return i;
    }
    string_t *copy = (s.len == 0) ? string_new(bc->alloc)
                                  : string_from_bytes(bc->alloc, s.ptr, s.len);
    vec_push(bc->strs, bc->alloc, copy);
    return vec_len(bc->strs) - 1;
}

/* ---- writer ---- */

void bcode_write_op(bytecode_t *bc, bcode_op_t op) {
    bcode_write_u32(bc, (uint32_t)op);
}

void bcode_write_u8(bytecode_t *bc, uint8_t v) {
    buf_append(bc, &v, 1);
}

void bcode_write_i8(bytecode_t *bc, int8_t v) {
    bcode_write_u8(bc, (uint8_t)v);
}

void bcode_write_u16(bytecode_t *bc, uint16_t v) {
    uint8_t buf[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
    buf_append(bc, buf, sizeof buf);
}

void bcode_write_i16(bytecode_t *bc, int16_t v) {
    bcode_write_u16(bc, (uint16_t)v);
}

void bcode_write_u32(bytecode_t *bc, uint32_t v) {
    uint8_t buf[4];
    put_u32(buf, v);
    buf_append(bc, buf, sizeof buf);
}

void bcode_write_i32(bytecode_t *bc, int32_t v) {
    bcode_write_u32(bc, (uint32_t)v);
}

void bcode_write_u64(bytecode_t *bc, uint64_t v) {
    uint8_t buf[8];
    for (int i = 0; i < 8; i++) buf[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
    buf_append(bc, buf, sizeof buf);
}

void bcode_write_i64(bytecode_t *bc, int64_t v) {
    bcode_write_u64(bc, (uint64_t)v);
}

void bcode_write_f32(bytecode_t *bc, float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof bits);
    bcode_write_u32(bc, bits);
}

void bcode_write_f64(bytecode_t *bc, double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    bcode_write_u64(bc, bits);
}

void bcode_write_bool(bytecode_t *bc, bool v) {
    bcode_write_u8(bc, v ? 1 : 0);
}

size_t bcode_write_str(bytecode_t *bc, strslice_t s) {
    size_t idx = bcode_str_index(bc, s);
    bcode_write_u32(bc, (uint32_t)idx);
    return idx;
}

size_t bcode_tell(const bytecode_t *bc) {
    return bc->code.len;
}

void bcode_patch_u32(bytecode_t *bc, size_t pos, uint32_t v) {
    if (pos + 4 > bc->code.len) {
        panic("bcode: patch position out of range");
    }
    put_u32(bc->code.data + pos, v);
}

/* ---- reader ---- */

static void bounds_check(const bytecode_t *bc, size_t pc, size_t n) {
    if (pc + n > bc->code.len) {
        panic("bcode: read out of bounds (pc=%zu, need=%zu, len=%zu)",
              pc, n, bc->code.len);
    }
}

bcode_op_t bcode_read_op(const bytecode_t *bc, size_t *pc) {
    return (bcode_op_t)bcode_read_u32(bc, pc);
}

uint8_t bcode_read_u8(const bytecode_t *bc, size_t *pc) {
    bounds_check(bc, *pc, 1);
    return bc->code.data[(*pc)++];
}

int8_t bcode_read_i8(const bytecode_t *bc, size_t *pc) {
    return (int8_t)bcode_read_u8(bc, pc);
}

uint16_t bcode_read_u16(const bytecode_t *bc, size_t *pc) {
    bounds_check(bc, *pc, 2);
    uint16_t v = (uint16_t)bc->code.data[*pc] |
                 ((uint16_t)bc->code.data[*pc + 1] << 8);
    *pc += 2;
    return v;
}

int16_t bcode_read_i16(const bytecode_t *bc, size_t *pc) {
    return (int16_t)bcode_read_u16(bc, pc);
}

uint32_t bcode_read_u32(const bytecode_t *bc, size_t *pc) {
    bounds_check(bc, *pc, 4);
    uint32_t v = get_u32(bc->code.data + *pc);
    *pc += 4;
    return v;
}

int32_t bcode_read_i32(const bytecode_t *bc, size_t *pc) {
    return (int32_t)bcode_read_u32(bc, pc);
}

uint64_t bcode_read_u64(const bytecode_t *bc, size_t *pc) {
    bounds_check(bc, *pc, 8);
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)bc->code.data[*pc + i] << (8 * i));
    *pc += 8;
    return v;
}

int64_t bcode_read_i64(const bytecode_t *bc, size_t *pc) {
    return (int64_t)bcode_read_u64(bc, pc);
}

float bcode_read_f32(const bytecode_t *bc, size_t *pc) {
    uint32_t bits = bcode_read_u32(bc, pc);
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

double bcode_read_f64(const bytecode_t *bc, size_t *pc) {
    uint64_t bits = bcode_read_u64(bc, pc);
    double v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

bool bcode_read_bool(const bytecode_t *bc, size_t *pc) {
    return bcode_read_u8(bc, pc) != 0;
}

strslice_t bcode_read_str(const bytecode_t *bc, size_t *pc) {
    uint32_t idx = bcode_read_u32(bc, pc);
    return bcode_str_at(bc, idx);
}

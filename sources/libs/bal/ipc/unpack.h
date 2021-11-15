#pragma once

#include <brutal/alloc.h>
#include <brutal/ds.h>

typedef struct
{
    void *buf;
    size_t len;
    size_t curr;
} BalUnpack;

typedef void BalUnpackFn(BalUnpack *unpack, void *dest, Alloc *alloc);

void bal_unpack_init(BalUnpack *self, void *buf, size_t len);

void bal_unpack(BalUnpack *self, void *buf, size_t len);

void bal_unpack_enum(BalUnpack *self, int *s);

void bal_unpack_s8(BalUnpack *self, int8_t *s);

void bal_unpack_u8(BalUnpack *self, uint8_t *u);

void bal_unpack_s16(BalUnpack *self, int16_t *s);

void bal_unpack_u16(BalUnpack *self, uint16_t *u);

void bal_unpack_s32(BalUnpack *self, int32_t *s);

void bal_unpack_u32(BalUnpack *self, uint32_t *u);

void bal_unpack_s64(BalUnpack *self, int64_t *s);

void bal_unpack_u64(BalUnpack *self, uint64_t *u);

void bal_unpack_f32(BalUnpack *self, float *f);

void bal_unpack_f64(BalUnpack *self, double *f);

void bal_unpack_str(BalUnpack *self, Str *str, Alloc *alloc);

void bal_unpack_slice_impl(BalUnpack *self, VoidSlice *slice, BalUnpackFn *el, size_t el_size, Alloc *alloc);

#define bal_unpack_slice(SELF, SLICE, EL, ALLOC) bal_unpack_slice_impl(SELF, (VoidSlice *)(SLICE), EL, sizeof(*(SLICE)->buf), ALLOC)

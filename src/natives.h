#pragma once
#include <stdint.h>
#include <string.h>

// rdr2 ps4 native handler abi: void handler(NativeCtx* ctx), ctx in rdi.
// ctx+0x00 = return slot ptr, ctx+0x10 = args array of 8-byte slots.
typedef struct {
    void*    ret;   // +0x00
    uint32_t argc;  // +0x08
    uint32_t _pad;  // +0x0c
    void*    args;  // +0x10
} NativeCtx;

typedef void (*native_fn)(NativeCtx*);

// generic native call. base = running eboot base, off = handler offset in offsets.h,
// a/n = up to 16 input slots (ped handles go in the low dword). returns the result slot.
static inline uint64_t nat_call(uint64_t base, uint64_t off, const uint64_t *a, uint32_t n) {
    uint64_t ret = 0;
    uint64_t argbuf[16];
    memset(argbuf, 0, sizeof(argbuf));
    for (uint32_t i = 0; i < n && i < 16; i++) argbuf[i] = a[i];
    NativeCtx ctx = {0};
    ctx.ret = &ret;
    ctx.argc = n;
    ctx.args = argbuf;
    ((native_fn)(base + off))(&ctx);
    return ret;
}
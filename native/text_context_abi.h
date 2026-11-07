#pragma once
#include <cstdint>
#include <cstddef>
// Mac 3.0.1 C ABI. Byte lengths are UTF-8, not NSString's UTF-16 positions.
struct TextAroundCursor {
    const char *before;uint32_t before_len;
    const char *after;uint32_t after_len;
    const char *on;uint32_t on_len;
    const char *clipboard;uint32_t clipboard_len;
    const char *extra;uint32_t extra_len;
};
static_assert(sizeof(TextAroundCursor)==80);
static_assert(offsetof(TextAroundCursor,after)==16);

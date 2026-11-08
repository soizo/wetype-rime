#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
// Mac 3.0.1: the official add/update wrapper supplies this 80-byte value.
// A null value pointer requests removal by native id. Empty id creates a word.
struct NativeUserWord {
    const char *id=nullptr;uint32_t id_len=0,pad0=0;
    const char *key=nullptr;uint32_t key_len=0,pad1=0;
    const char *value=nullptr;uint32_t value_len=0,type=0;
    const char *extra=nullptr;uint32_t extra_len=0,pad2=0;
    uint64_t time=0;uint32_t reserved=0,flags=0;
};
static_assert(sizeof(NativeUserWord)==80);
static_assert(offsetof(NativeUserWord,value)==32);
struct UserWord {std::string id,key,value;};

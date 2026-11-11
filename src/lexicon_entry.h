#pragma once
#include <cstdint>
#include <string>
namespace wetype {
struct LexiconEntry {std::string code,text;double weight=0;uint32_t initial_count=0;};
}

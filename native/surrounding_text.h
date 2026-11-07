#pragma once
#include <string>
namespace wetype {
struct SurroundingText {bool available=false;std::string before;};
bool install_imk_context();
SurroundingText surrounding_text();
}

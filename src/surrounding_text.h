#pragma once
#include <string>
namespace wetype {
struct SurroundingText {bool available=false;std::string before,after,on;};
bool install_imk_context();
SurroundingText surrounding_text();
}

#include "../native/session_options.h"
#include <cstdio>
#include <stdexcept>
int main(){
    for(unsigned flags=0;flags<16;++flags){
        unsigned char config[312]={};config[54]=1;wetype_session_expressions(config,flags);
        for(unsigned i=0;i<sizeof(config);++i){
            unsigned expected=i==45||i==52?bool(flags&WETYPE_UNICODE_EMOJI):
                i==46||i==53?bool(flags&WETYPE_KAOMOJI):
                i==57?bool(flags&WETYPE_SYMBOL_EMOJI):0;
            if(config[i]!=expected)throw std::runtime_error("expression options changed an unrelated session field");
        }
    }
    puts("PASS: independent local expression flags; Emoji Plus forced off; cloud, WeChat, AI and unrelated fields untouched");
}

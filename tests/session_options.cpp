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
    for(unsigned flags=0;flags<4;++flags){
        unsigned char config[312]={};wetype_session_wubi(config,flags);
        for(unsigned i=0;i<sizeof(config);++i){
            unsigned expected=i==36?bool(flags&1):i==39||i==40?bool(flags&2):i==41?1:0;
            if(config[i]!=expected)throw std::runtime_error("Wubi options changed an unrelated session field");
        }
    }
    puts("PASS: independent local expression flags; Emoji Plus forced off; cloud, WeChat, AI and unrelated fields untouched");
}

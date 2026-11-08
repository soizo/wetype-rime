// Verify Rime's actual derive/xform implementation, without vendor resources.
#include "../src/rime_fuzzy.h"
#include <cassert>
#include <iostream>

static rime::an<rime::ConfigList> list(std::initializer_list<const char *> values){
    auto result=rime::New<rime::ConfigList>();
    for(auto value:values)result->Append(rime::New<rime::ConfigValue>(value));
    return result;
}
int main(){
    rime::Config config;
    config.SetItem("wetype_fuzzy/syllables",list({"jin","jing","xin","xing","yu","an","ang"}));
    config.SetItem("speller/algebra",list({"derive/in$/ing/","derive/ing$/in/"}));
    config.SetItem("wetype_fuzzy/flypy",list({"xform/ing$/K/","xform/in$/B/","xform/(.)an$/$1J/","xlit/BKJ/bkj/"}));
    wetype::RimeFuzzy fuzzy;
    fuzzy.load(&config,"quanpin");
    assert(fuzzy.variants("jin")==std::vector<std::string>{"jing"});
    assert(fuzzy.variants("xing")==std::vector<std::string>{"xin"});
    assert(fuzzy.variants("jin'yu")==std::vector<std::string>{"jing'yu"});
    assert(fuzzy.variants("an").empty()); // not a hardcoded fuzzy pair
    assert(fuzzy.variants("OpenAI").empty());
    assert(fuzzy.variants("jin'").empty());
    assert(fuzzy.variants(std::string(33,'a')).empty());
    fuzzy.load(&config,"flypy");
    assert(fuzzy.variants("jb")==std::vector<std::string>{"jing"});
    assert(fuzzy.variants("xk")==std::vector<std::string>{"xin"});
    assert(fuzzy.variants("jbyu")==std::vector<std::string>{"jing'yu"});
    config.SetItem("speller/algebra",list({"derive/an$/ang/","derive/ang$/an/"}));
    fuzzy.load(&config,"quanpin");
    assert(fuzzy.variants("jin").empty());
    assert(fuzzy.variants("an")==std::vector<std::string>{"ang"});
    config.SetItem("speller/algebra",list({"not-a-rime-rule"}));
    bool rejected=false;
    try{fuzzy.load(&config,"quanpin");}catch(const std::runtime_error &){rejected=true;}
    assert(rejected);
    fuzzy.load(&config,"wubi86");
    assert(fuzzy.variants("an").empty());
    std::cout<<"PASS: real Rime algebra, Xiaohe projection, phrase boundaries, editable rules and bounded fallback\n";
}

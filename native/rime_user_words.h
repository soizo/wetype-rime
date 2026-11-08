#pragma once
#include <rime/config.h>
#include <rime/dict/db.h>
#include <rime/dict/user_db.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wetype {
struct RimeUserWords {
    std::string source;
    std::vector<std::pair<std::string,std::string>> entries;
    void load(rime::Config *config){
        source.clear();entries.clear();
        std::string name,db_class="stabledb";
        if(!config->GetString("wetype_user_words/user_dict",&name)||name.empty())return;
        config->GetString("wetype_user_words/db_class",&db_class);
        if(db_class!="stabledb")throw std::runtime_error("WeType user words require Rime stabledb");
        if(name.size()>128||name.find("..")!=std::string::npos||name.front()=='/'||
           name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.")!=std::string::npos)
            throw std::runtime_error("invalid Rime user dictionary name");
        auto component=rime::Db::Require(db_class);
        if(!component)throw std::runtime_error("Rime stabledb component unavailable");
        std::unique_ptr<rime::Db> db(component->Create(name));
        source=db->file_path().u8string();
        bool enabled=true;config->GetBool("wetype_user_words/enabled",&enabled);
        if(!enabled||!db->Exists())return;
        if(!db->OpenReadOnly())throw std::runtime_error("cannot read Rime user dictionary");
        auto rows=db->QueryAll();
        if(!rows)throw std::runtime_error("cannot enumerate Rime user dictionary");
        std::string key,value;size_t bytes=0;
        while(rows->GetNextRecord(&key,&value)){
            auto separator=key.find('\t');
            if(separator==std::string::npos)throw std::runtime_error("invalid Rime user-word record");
            auto code=key.substr(0,separator),text=key.substr(separator+1);
            code.erase(std::remove(code.begin(),code.end(),' '),code.end());
            if(rime::UserDbValue(value).commits<0)continue;
            bytes+=code.size()+text.size();
            if(code.empty()||code.size()>128||text.empty()||text.size()>4096||text.find('\0')!=std::string::npos||
               code.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';")!=std::string::npos||
               entries.size()>=2048||bytes>2*1024*1024)
                throw std::runtime_error("invalid or oversized Rime user-word table");
            entries.emplace_back(std::move(code),std::move(text));
        }
    }
};
}

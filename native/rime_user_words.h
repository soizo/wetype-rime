#pragma once
#include <rime/config.h>
#include <rime/dict/db.h>
#include <rime/dict/user_db.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wetype {
struct RimeUserWords {
    std::string source;
    std::vector<std::pair<std::string,std::string>> entries;
    std::map<std::pair<std::string,std::string>,int> weights;
    double initial_quality=99;
    std::optional<int> priority(const std::string &code,const std::string &text) const {
        if(initial_quality<=0)return {};
        auto at=weights.find({code,text});
        return at==weights.end()?std::optional<int>():at->second;
    }
    void load(rime::Config *config){
        source.clear();entries.clear();weights.clear();initial_quality=99;
        config->GetDouble("custom_phrase/initial_quality",&initial_quality);
        if(!std::isfinite(initial_quality))throw std::runtime_error("invalid custom_phrase initial_quality");
        std::string name,db_class="stabledb";
        if(!config->GetString("custom_phrase/user_dict",&name)||name.empty())return;
        config->GetString("custom_phrase/db_class",&db_class);
        bool enabled=true;config->GetBool("custom_phrase/enable_user_dict",&enabled);
        // An explicitly configured official table translator owns its own
        // namespace. Do not also import its phrases into the native store.
        bool delegated=false;
        if(auto translators=config->GetList("engine/translators")){
            for(size_t i=0;i<translators->size();++i){
                auto entry=translators->GetValueAt(i);
                if(entry&&entry->str()=="table_translator@custom_phrase")delegated=true;
            }
        }
        if(enabled&&!delegated&&db_class!="stabledb")throw std::runtime_error("native custom_phrase requires Rime stabledb");
        rime::path relative(name);
        if(name.size()>1024||name.find('\0')!=std::string::npos||relative.is_absolute())
            throw std::runtime_error("invalid Rime user dictionary name");
        for(const auto &part:relative)if(part=="..")throw std::runtime_error("invalid Rime user dictionary name");
        auto component=rime::Db::Require("stabledb");
        if(!component)throw std::runtime_error("Rime stabledb component unavailable");
        std::unique_ptr<rime::Db> db(component->Create(name));
        source=db->file_path().u8string();
        if(!enabled||delegated||!db->Exists())return;
        std::string dictionary;
        if(config->GetString("custom_phrase/dictionary",&dictionary)&&!dictionary.empty())
            throw std::runtime_error("native custom_phrase requires an empty dictionary setting");
        if(!db->OpenReadOnly())throw std::runtime_error("cannot read Rime user dictionary");
        auto rows=db->QueryAll();
        if(!rows)throw std::runtime_error("cannot enumerate Rime user dictionary");
        std::string key,value;size_t bytes=0;
        while(rows->GetNextRecord(&key,&value)){
            auto separator=key.find('\t');
            if(separator==std::string::npos)throw std::runtime_error("invalid Rime user-word record");
            auto code=key.substr(0,separator),text=key.substr(separator+1);
            code.erase(std::remove(code.begin(),code.end(),' '),code.end());
            int weight=rime::UserDbValue(value).commits;
            if(weight<0)continue;
            bytes+=code.size()+text.size();
            if(code.empty()||code.size()>128||text.empty()||text.size()>4096||text.find('\0')!=std::string::npos||
               code.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';")!=std::string::npos||
               entries.size()>=2048||bytes>2*1024*1024)
                throw std::runtime_error("invalid or oversized Rime user-word table");
            weights[{code,text}]=weight;
            entries.emplace_back(std::move(code),std::move(text));
        }
    }
};
}

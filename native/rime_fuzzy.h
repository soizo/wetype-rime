#ifndef WETYPE_RIME_FUZZY_H
#define WETYPE_RIME_FUZZY_H
#include <rime/algo/algebra.h>
#include <algorithm>
#include <set>
#include <sstream>

namespace wetype {
// Rime spelling algebra maps canonical syllables to accepted input spellings.
// Keep that direction (derive is not a replacement of the user's input).
class RimeFuzzy {
    rime::Script spellings;
    size_t max_code=0;
public:
    void load(rime::Config *config,const std::string &mode){
        spellings.clear();max_code=0;
        if(mode!="quanpin"&&mode!="flypy")return;
        auto syllables=config->GetList("wetype_fuzzy/syllables");
        auto rules=config->GetList("speller/algebra");
        if(!syllables||!rules)throw std::runtime_error("Rime fuzzy syllables/algebra missing");
        if(syllables->size()==0||syllables->size()>1024)throw std::runtime_error("invalid Rime fuzzy syllable count");
        for(size_t i=0;i<syllables->size();++i){
            auto item=syllables->GetValueAt(i);
            if(!item||item->str().empty()||item->str().size()>6||item->str().find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos)
                throw std::runtime_error("invalid Rime fuzzy syllable");
            spellings.AddSyllable(item->str());
        }
        if(spellings.size()>1024)throw std::runtime_error("too many Rime fuzzy syllables");
        rime::Projection algebra;
        if(!algebra.Load(rules))throw std::runtime_error("invalid Rime speller/algebra");
        algebra.Apply(&spellings);
        // Plain derive in Rime keeps kNormalSpelling. Classify derived input
        // aliases before the layout projection changes every spelling's code.
        for(auto &entry:spellings)for(auto &spelling:entry.second)
            if(entry.first!=spelling.str&&spelling.properties.type==rime::kNormalSpelling)
                spelling.properties.type=rime::kFuzzySpelling;
        if(mode=="flypy"){
            rime::Projection layout;
            if(!layout.Load(config->GetList("wetype_fuzzy/flypy")))throw std::runtime_error("invalid Rime fuzzy Xiaohe layout");
            layout.Apply(&spellings);
        }
        if(spellings.size()>8192)throw std::runtime_error("Rime fuzzy algebra expands too many spellings");
        for(const auto &item:spellings)max_code=std::max(max_code,item.first.size());
    }

    std::vector<std::string> variants(const std::string &input) const {
        // Bound work on the frontend thread. Partial syllables, English,
        // unsupported layouts and long compositions keep the native path.
        if(spellings.empty()||input.empty()||input.size()>32||input.back()=='\''||
           input.find_first_not_of("abcdefghijklmnopqrstuvwxyz'")!=std::string::npos)return {};
        struct Path{std::string pinyin;unsigned fuzzy=0,syllables=0;};
        std::vector<std::vector<Path>> paths(input.size()+1);
        paths[0].push_back({});
        auto trim=[](std::vector<Path> &values){
            std::sort(values.begin(),values.end(),[](const Path &a,const Path &b){
                if(a.syllables!=b.syllables)return a.syllables<b.syllables;
                if(a.fuzzy!=b.fuzzy)return a.fuzzy<b.fuzzy;
                return a.pinyin<b.pinyin;
            });
            values.erase(std::unique(values.begin(),values.end(),[](const Path&a,const Path&b){return a.pinyin==b.pinyin;}),values.end());
            if(values.size()>32)values.resize(32);
        };
        for(size_t pos=0;pos<input.size();++pos){
            trim(paths[pos]);
            if(input[pos]=='\''){
                if(pos&&input[pos-1]!='\'')for(const auto &path:paths[pos])paths[pos+1].push_back(path);
                continue;
            }
            for(size_t end=pos+1;end<=input.size()&&end-pos<=max_code;++end){
                if(input[end-1]=='\'')break;
                auto found=spellings.find(input.substr(pos,end-pos));
                if(found==spellings.end())continue;
                for(const auto &path:paths[pos])for(const auto &spelling:found->second){
                    if(spelling.properties.type!=rime::kNormalSpelling&&spelling.properties.type!=rime::kFuzzySpelling)continue;
                    unsigned fuzzy=path.fuzzy+(spelling.properties.type==rime::kFuzzySpelling);
                    if(fuzzy>2||path.syllables>=12)continue;
                    paths[end].push_back({path.pinyin+(path.pinyin.empty()?"":"'")+spelling.str,fuzzy,path.syllables+1});
                }
                trim(paths[end]);
            }
        }
        trim(paths.back());
        std::vector<std::string> result;
        for(const auto &path:paths.back())if(path.fuzzy){
            result.push_back(path.pinyin);
            if(result.size()==3)break;
        }
        return result;
    }
};
}
#endif

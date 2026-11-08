#pragma once
#include <rime/config.h>
#include <rime/dict/dict_settings.h>
#include <rime/dict/entry_collector.h>
#include <rime/resource.h>
#include <rime/service.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include "lexicon_entry.h"

namespace wetype {
struct RimeLexicon {
    std::vector<LexiconEntry> entries;
    static void check_name(const std::string &name){
        rime::path relative(name);
        if(name.empty()||name.size()>1024||relative.is_absolute()||name.find('\0')!=std::string::npos)
            throw std::runtime_error("invalid Rime dictionary name");
        for(const auto &part:relative)if(part=="..")throw std::runtime_error("invalid Rime dictionary name");
    }
    void load(rime::Config *config){
        entries.clear();std::string name;
        if(!config->GetString("translator/dictionary",&name)||name.empty())return;
        check_name(name);
        auto resolver=rime::Service::instance().CreateResourceResolver({"source_file","",""});
        auto root=resolver->ResolvePath(name+".dict.yaml");
        rime::DictSettings settings;std::ifstream input(root);
        if(!input||!settings.LoadDictHeader(input)||settings.dict_name()!=name)
            throw std::runtime_error("cannot read Rime dictionary header");
        auto tables=settings.GetTables();
        if(!tables||tables->size()>128)throw std::runtime_error("invalid Rime import_tables");
        std::vector<rime::path> files;uintmax_t bytes=0;
        for(size_t i=0;i<tables->size();++i){
            auto value=tables->GetValueAt(i);
            if(!value)throw std::runtime_error("invalid Rime import table");
            check_name(value->str());auto path=resolver->ResolvePath(value->str()+".dict.yaml");
            if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Rime import table missing");
            bytes+=std::filesystem::file_size(path);
            if(bytes>16*1024*1024)throw std::runtime_error("Rime dictionary sources exceed 16 MiB");
            rime::DictSettings header;std::ifstream file(path);
            if(!header.LoadDictHeader(file))throw std::runtime_error("invalid Rime import table header");
            files.push_back(path);
        }
        // Use librime's own columns, weight parsing and phrase encoder.
        rime::EntryCollector collector;collector.Configure(&settings);collector.Collect(files);
        if(collector.entries.size()>10000)throw std::runtime_error("Rime dictionary exceeds 10000 entries");
        for(const auto &entry:collector.entries){
            if(!std::isfinite(entry->weight)||entry->weight<0||entry->weight>1e12)
                throw std::runtime_error("invalid Rime dictionary weight");
            std::string code;
            for(const auto &syllable:entry->raw_code){
                if(syllable.empty()||syllable.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos)
                    throw std::runtime_error("ordinary native dictionaries require full pinyin syllables");
                if(!code.empty())code+=',';code+=syllable;
            }
            if(code.empty()||code.size()>128||entry->text.empty()||entry->text.size()>4096)
                throw std::runtime_error("invalid Rime dictionary entry");
            entries.push_back({std::move(code),entry->text,entry->weight});
        }
    }
};
}

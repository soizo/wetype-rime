#pragma once
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>

// The native engine owns binary cell parsing, ranking and dictionary lifetimes.
// Rime selects catalogue identifiers; it never supplies arbitrary binary paths.
class CellDictionaries {
public:
    using Add=bool (*)(const char *,uint32_t,void *);
    using Clear=void (*)();
    struct Entry {std::string path;uint32_t type=0;};
private:
    std::map<std::string,Entry> catalogue;
    std::vector<std::string> active;
    std::string catalogue_error;
    Add add=nullptr;Clear clear=nullptr;
    void install(const std::vector<std::string> &ids){
        clear();
        for(const auto &id:ids){const auto &entry=catalogue.at(id);
            if(!add(entry.path.c_str(),entry.type,nullptr))throw std::runtime_error("native cell dictionary rejected");}
    }
public:
    void initialise(const char *file,Add add_function,Clear clear_function){
        add=add_function;clear=clear_function;
        if(!file||!*file)return;
        try{
            std::ifstream input(file);std::string line;
            if(!input)throw std::runtime_error("native cell catalogue missing; reinstall the adapter");
            std::map<std::string,Entry> loaded;
            while(std::getline(input,line)){
                auto first=line.find('\t'),second=first==std::string::npos?first:line.find('\t',first+1);
                if(first==std::string::npos||second==std::string::npos||line.find('\t',second+1)!=std::string::npos)
                    throw std::runtime_error("invalid native cell catalogue");
                auto id=line.substr(0,first),path=line.substr(first+1,second-first-1);
                uint64_t type=0;std::string extra;std::istringstream value(line.substr(second+1));
                if(!valid_id(id)||path.empty()||path.front()!='/'||path.find('\0')!=std::string::npos||
                   !(value>>type)||value>>extra||type>UINT32_MAX||loaded.count(id)||loaded.size()>=32)
                    throw std::runtime_error("invalid native cell catalogue entry");
                loaded[id]={path,static_cast<uint32_t>(type)};
            }
            if(!input.eof())throw std::runtime_error("cannot read native cell catalogue");
            catalogue=std::move(loaded);
        }catch(const std::exception &e){catalogue_error=e.what();}
    }
    static bool valid_id(const std::string &id){
        return !id.empty()&&id.size()<=128&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos;
    }
    const auto &available() const {return catalogue;}
    const auto &enabled() const {return active;}
    bool sync(const std::vector<std::string> &ids){
        if(ids.size()>32)throw std::runtime_error("too many native cell dictionaries");
        if(!ids.empty()&&!catalogue_error.empty())throw std::runtime_error(catalogue_error);
        std::set<std::string> seen;
        for(const auto &id:ids){
            if(!valid_id(id)||!seen.insert(id).second)throw std::runtime_error("invalid or duplicate native cell identifier");
            auto at=catalogue.find(id);
            if(at==catalogue.end())throw std::runtime_error("native cell identifier not present in this engine");
            struct stat info{};
            if(stat(at->second.path.c_str(),&info)||!S_ISREG(info.st_mode)||!info.st_size)
                throw std::runtime_error("native cell resource missing or empty");
        }
        if(ids==active)return false;
        if(!add||!clear)throw std::runtime_error("native cell API unavailable in this engine");
        try{install(ids);}
        catch(...){
            try{install(active);}catch(...){active.clear();throw std::runtime_error("native cell dictionary rollback failed");}
            throw;
        }
        active=ids;return true;
    }
};

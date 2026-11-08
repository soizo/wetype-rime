#pragma once
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <dlfcn.h>
#include <fstream>
#include <cmath>
#include <ctime>
#include <memory>
#include <string_view>
#include "lexicon_entry.h"

// Private ABI is deliberately confined to the isolated macOS engine worker.
// Resolve local Mach-O symbols by name rather than per-build code addresses.
class NativeLexicon {
    struct Completion {
        uintptr_t storage[4]={};
        Completion()=default;
        Completion(const Completion &)=delete;
        ~Completion(){
            auto target=reinterpret_cast<void *>(storage[3]);
            if(target){auto table=*reinterpret_cast<void ***>(target);
                reinterpret_cast<void (*)(void *)>(table[target==this?4:5])(target);}
        }
        void wait(){auto target=reinterpret_cast<void *>(storage[3]);if(target){
            auto table=*reinterpret_cast<void ***>(target);
            reinterpret_cast<void (*)(void *)>(table[6])(target);}}
    };
    static_assert(sizeof(Completion)==32);
    struct Present {uint32_t timestamp;bool present;};
    using Pair=std::pair<std::string,std::string>;
    struct Record {double weight=0;bool owned=false,learned=false,installed=true;};
    std::map<Pair,Record> records;
    std::string ledger;
    void *(*instance)()=nullptr;
    void *(*unwrap)(void *)=nullptr;
    std::shared_ptr<void> (*get_dict)(void *,uint16_t)=nullptr;
    Completion (*add)(void *,std::string_view,std::string_view,const void *)=nullptr;
    Completion (*remove)(void *,std::string_view,std::string_view)=nullptr;
    Present (*lookup)(void *,std::string_view,std::string_view)=nullptr;
    bool loaded=false;
    template<class T> static void bind(T &fn,const std::map<std::string,uintptr_t> &symbols,const char *name){
        auto at=symbols.find(name);
        if(at==symbols.end())throw std::runtime_error("ordinary dictionary ABI unavailable in this engine");
        fn=reinterpret_cast<T>(at->second);
    }
    std::shared_ptr<void> dictionary(){
        void *wrapper=unwrap(instance());
        if(!wrapper)throw std::runtime_error("native dictionary manager unavailable");
        auto holder=**reinterpret_cast<void ***>(wrapper);
        if(!holder)throw std::runtime_error("native dictionary manager unavailable");
        auto result=get_dict(holder,128);
        if(!result)throw std::runtime_error("native pinyin user dictionary unavailable");
        return result;
    }
    void save(){
        auto temporary=ledger+".tmp";
        std::ofstream out(temporary,std::ios::trunc);
        if(!out)throw std::runtime_error("cannot save dictionary import ledger");
        out<<"WETYPE_LEXICON_2\n"<<std::setprecision(17);
        for(const auto &item:records)out<<"U "<<hex(item.first.first)<<' '<<hex(item.first.second)<<' '
            <<item.second.weight<<' '<<item.second.owned<<' '<<item.second.learned<<' '<<item.second.installed<<'\n';
        out.close();
        if(!out||rename(temporary.c_str(),ledger.c_str()))throw std::runtime_error("cannot replace dictionary import ledger");
    }
    void persist(const Pair &key){
        struct stat status{};
        if(stat(ledger.c_str(),&status)!=0||status.st_size>8*1024*1024){save();return;}
        std::ofstream out(ledger,std::ios::app);
        auto at=records.find(key);
        out<<std::setprecision(17)<<(at==records.end()?"D ":"U ")<<hex(key.first)<<' '<<hex(key.second);
        if(at!=records.end())out<<' '<<at->second.weight<<' '<<at->second.owned<<' '<<at->second.learned<<' '<<at->second.installed;
        out<<'\n';out.close();
        if(!out)throw std::runtime_error("cannot update dictionary import ledger");
    }
public:
    using Entry=wetype::LexiconEntry;
    void initialise(void *initialise,const char *path,const char *work){
        ledger=std::string(work)+"/rime-lexicon.state";
        Dl_info info{};
        if(!dladdr(initialise,&info)||!info.dli_fbase)throw std::runtime_error("cannot locate native engine image");
        auto header=static_cast<const mach_header_64 *>(info.dli_fbase);
        if(header->magic!=MH_MAGIC_64||header->cputype!=CPU_TYPE_ARM64)throw std::runtime_error("unsupported dictionary engine architecture");
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        auto size=file.tellg();
        if(size<=0||size>512*1024*1024)throw std::runtime_error("invalid dictionary engine image");
        std::vector<char> bytes(static_cast<size_t>(size));file.seekg(0);file.read(bytes.data(),bytes.size());
        if(!file||bytes.size()<sizeof(mach_header_64))throw std::runtime_error("cannot read dictionary engine symbols");
        auto disk=reinterpret_cast<const mach_header_64 *>(bytes.data());
        if(disk->magic!=MH_MAGIC_64||disk->sizeofcmds>bytes.size()-sizeof(*disk))throw std::runtime_error("invalid dictionary engine header");
        const symtab_command *symtab=nullptr;uint64_t text_vm=0;size_t offset=sizeof(*disk);
        for(uint32_t i=0;i<disk->ncmds;++i){
            if(offset+sizeof(load_command)>sizeof(*disk)+disk->sizeofcmds)throw std::runtime_error("invalid engine load commands");
            auto command=reinterpret_cast<const load_command *>(bytes.data()+offset);
            if(command->cmdsize<sizeof(*command)||command->cmdsize>sizeof(*disk)+disk->sizeofcmds-offset)throw std::runtime_error("invalid engine load command");
            if(command->cmd==LC_SYMTAB&&command->cmdsize>=sizeof(symtab_command))symtab=reinterpret_cast<const symtab_command *>(command);
            if(command->cmd==LC_SEGMENT_64&&command->cmdsize>=sizeof(segment_command_64)){
                auto segment=reinterpret_cast<const segment_command_64 *>(command);
                if(strncmp(segment->segname,"__TEXT",16)==0)text_vm=segment->vmaddr;
            }
            offset+=command->cmdsize;
        }
        if(!symtab||!text_vm||uint64_t(symtab->symoff)+uint64_t(symtab->nsyms)*sizeof(nlist_64)>bytes.size()||
           uint64_t(symtab->stroff)+symtab->strsize>bytes.size())throw std::runtime_error("ordinary dictionary symbols unavailable");
        std::map<std::string,uintptr_t> symbols;
        auto list=reinterpret_cast<const nlist_64 *>(bytes.data()+symtab->symoff);
        for(uint32_t i=0;i<symtab->nsyms;++i){
            const auto &symbol=list[i];
            if((symbol.n_type&N_TYPE)!=N_SECT||symbol.n_un.n_strx>=symtab->strsize||symbol.n_value<text_vm)continue;
            auto name=bytes.data()+symtab->stroff+symbol.n_un.n_strx;
            auto length=strnlen(name,symtab->strsize-symbol.n_un.n_strx);
            if(length==symtab->strsize-symbol.n_un.n_strx)continue;
            // Only the six required symbols are retained from the local image.
            if(strstr(name,"ResourceMgr8InstanceEv")||strstr(name,"ResourceMgrdeEv")||
               strstr(name,"DictManager19GetSharedDictByKindEt")||strstr(name,"10UserDictV23AddE")||
               strstr(name,"10UserDictV26RemoveE")||strstr(name,"10UserDictV212LookUpByPairE"))
                symbols.emplace(std::string(name,length),reinterpret_cast<uintptr_t>(info.dli_fbase)+symbol.n_value-text_vm);
        }
        bind(instance,symbols,"__ZN5wxime6engine9resources11ResourceMgr8InstanceEv");
        bind(unwrap,symbols,"__ZNR5wxime6engine9resources11ResourceMgrdeEv");
        bind(get_dict,symbols,"__ZNK5wxime4data4dict11DictManager19GetSharedDictByKindEt");
        bind(add,symbols,"__ZN5wxime4data4dict10UserDictV23AddENSt3__117basic_string_viewIcNS3_11char_traitsIcEEEES7_RKNS1_17UserDictAddOptionE");
        bind(remove,symbols,"__ZN5wxime4data4dict10UserDictV26RemoveENSt3__117basic_string_viewIcNS3_11char_traitsIcEEEES7_");
        bind(lookup,symbols,"__ZNK5wxime4data4dict10UserDictV212LookUpByPairENSt3__117basic_string_viewIcNS3_11char_traitsIcEEEES7_");
        std::ifstream state(ledger);std::string version;
        if(state){
            std::getline(state,version);
            if(version!="WETYPE_LEXICON_2")throw std::runtime_error("invalid dictionary import ledger");
            std::string line;
            while(std::getline(state,line)){
                std::istringstream in(line);std::string tag,code,text,extra;Record row;
                if(!(in>>tag>>code>>text))throw std::runtime_error("invalid dictionary import ledger");
                auto key=Pair{unhex(code),unhex(text)};
                if(tag=="D")records.erase(key);
                else if(tag=="U"&&in>>row.weight>>row.owned>>row.learned>>row.installed&&std::isfinite(row.weight))records[key]=row;
                else throw std::runtime_error("invalid dictionary import ledger");
                if(in>>extra||records.size()>10000)throw std::runtime_error("invalid dictionary import ledger");
            }
            if(!state.eof())throw std::runtime_error("invalid dictionary import ledger");
        }
        loaded=true;
    }
    bool ready() const {return loaded;}
    // Scores are time-domain values in this ABI. Use a bounded logarithmic
    // initial bias, rather than treating a Rime frequency as a native timestamp.
    static float initial_score(double weight){
        return static_cast<float>(std::time(nullptr)+3600*std::min(336.0,std::log2(1+weight)));
    }
    void sync(const std::vector<Entry> &entries){
        if(!loaded)throw std::runtime_error("ordinary dictionary API unavailable");
        auto dict=dictionary();std::map<Pair,double> desired;
        for(const auto &entry:entries){auto key=Pair{entry.code,entry.text};
            auto at=desired.find(key);if(at==desired.end()||at->second<entry.weight)desired[key]=entry.weight;}
        for(auto it=records.begin();it!=records.end();){
            auto wanted=desired.find(it->first);
            bool changed=wanted!=desired.end()&&wanted->second!=it->second.weight;
            if(wanted==desired.end()||changed){
                if(it->second.owned&&!it->second.learned)remove(dict.get(),it->first.first,it->first.second).wait();
                // Existing learning is never overwritten by a new initial weight.
                if(changed&&(!it->second.owned||it->second.learned)){
                    it->second.weight=wanted->second;persist(it->first);++it;
                }else{auto key=it->first;it=records.erase(it);persist(key);}
            }else ++it;
        }
        for(const auto &item:desired){
            auto old=records.find(item.first);
            if(old!=records.end()&&old->second.installed)continue;
            bool existing=lookup(dict.get(),item.first.first,item.first.second).present;
            if(old==records.end()){
                records[item.first]={item.second,!existing,existing,existing};
                persist(item.first);
            }
            if(!existing){
                alignas(8) unsigned char option[24]={};float score=initial_score(item.second);memcpy(option+4,&score,sizeof(score));
                add(dict.get(),item.first.first,item.first.second,option).wait();
            }
            records[item.first].installed=true;persist(item.first);
        }
    }
    // The worker is the sole writer. Keep selected vocabulary as learned words
    // when its source is disabled, just as a Rime static dictionary/userdb pair.
    void learn(const std::string &text){
        for(auto &item:records)if(!item.second.learned&&text.find(item.first.second)!=std::string::npos){
            item.second.learned=true;persist(item.first);}
    }
};

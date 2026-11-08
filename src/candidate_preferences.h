#pragma once
#include <rime/service.h>
#include <sys/file.h>
#include <fstream>
#include <set>
#include <cstdio>
#include "rime_paths.h"
namespace wetype {
// A locked, atomic local snapshot shared by schemas, sessions and module images.
class CandidatePreferences {
    using Key=std::pair<string,string>;
    string file,scope;
    int fd=-1;
    std::map<Key,string> pins;
    std::set<std::pair<Key,string>> blocks;
public:
    explicit CandidatePreferences(const string &mode):scope(mode){
        file=user_path("wetype/state/candidates");
        std::filesystem::create_directories(std::filesystem::path(file).parent_path());
        fd=::open((file+".lock").c_str(),O_CREAT|O_RDWR,0600);
        if(fd<0||flock(fd,LOCK_EX)){if(fd>=0)close(fd);throw std::runtime_error("cannot lock candidate preferences");}
        try{
            // Import the previous snapshot once, under its original lock.
            auto legacy=user_path("wetype-candidates.state");
            if(!std::filesystem::exists(file)&&std::filesystem::exists(legacy)){
                int old=::open((legacy+".lock").c_str(),O_CREAT|O_RDWR,0600);
                if(old<0)throw std::runtime_error("cannot open legacy candidate preferences");
                if(flock(old,LOCK_EX)){close(old);throw std::runtime_error("cannot lock legacy candidate preferences");}
                std::error_code ec;
                std::filesystem::copy_file(legacy,file,ec);
                flock(old,LOCK_UN);close(old);
                if(ec)throw std::runtime_error("cannot migrate candidate preferences");
            }
            std::ifstream in(file);string line;size_t count=0;
            while(std::getline(in,line)){
                std::istringstream row(line);string tag,mode,code,text,extra;
                if(!(row>>tag>>mode>>code>>text)||row>>extra||++count>10000)throw std::runtime_error("invalid candidate preferences");
                Key key{unhex(mode),unhex(code)};auto value=unhex(text);
                if(tag=="P")pins[key]=value;
                else if(tag=="B")blocks.insert({key,value});
                else throw std::runtime_error("invalid candidate preference action");
            }
        }catch(...){flock(fd,LOCK_UN);close(fd);fd=-1;throw;}
    }
    ~CandidatePreferences(){if(fd>=0){flock(fd,LOCK_UN);close(fd);}}
    bool blocked(const string &code,const string &text)const{return blocks.count({{scope,code},text});}
    bool pinned(const string &code,const string &text)const{auto it=pins.find({scope,code});return it!=pins.end()&&it->second==text;}
    void change(const string &action,const string &code,const string &text){
        Key key{scope,code};
        if(action=="pin"){if(pinned(code,text))pins.erase(key);else {pins[key]=text;blocks.erase({key,text});}}
        else if(action=="block"){blocks.insert({key,text});if(pinned(code,text))pins.erase(key);}
        else if(action=="unpin")pins.erase(key);
        else if(action=="restore"){
            for(auto it=blocks.begin();it!=blocks.end();)if(it->first==key)it=blocks.erase(it);else ++it;
        }else throw std::runtime_error("unknown candidate action");
        if(pins.size()+blocks.size()>10000)throw std::runtime_error("too many candidate preferences");
        string temporary=file+".tmp";
        int out=::open(temporary.c_str(),O_CREAT|O_TRUNC|O_WRONLY,0600);
        if(out<0)throw std::runtime_error("cannot save candidate preferences");
        string data;
        for(auto &p:pins)data+="P "+hex(p.first.first)+" "+hex(p.first.second)+" "+hex(p.second)+"\n";
        for(auto &b:blocks)data+="B "+hex(b.first.first)+" "+hex(b.first.second)+" "+hex(b.second)+"\n";
        size_t at=0;bool ok=true;
        while(at<data.size()){auto n=::write(out,data.data()+at,data.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}at+=n;}
        if(fsync(out))ok=false;close(out);
        if(!ok||rename(temporary.c_str(),file.c_str())){unlink(temporary.c_str());throw std::runtime_error("cannot replace candidate preferences");}
    }
};
}

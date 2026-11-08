#pragma once
#include <spawn.h>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
extern char **environ;
namespace wetype {
using std::string;
using Clock=std::chrono::steady_clock;
static string hex(const string &s) {
    static const char *digits="0123456789abcdef";string r;
    for(unsigned char c:s){r+=digits[c>>4];r+=digits[c&15];}return r.empty()?"-":r;
}
static string unhex(const string &s) {
    if(s=="-")return {};
    if(s.size()%2||s.size()>8192)throw std::runtime_error("invalid engine payload");
    auto nib=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("invalid engine hex");};
    string r;for(size_t i=0;i<s.size();i+=2)r+=char((nib(s[i])<<4)|nib(s[i+1]));return r;
}
struct Row{string text;size_t cover,index;string preedit;};
struct Result{uint64_t ticket;std::vector<Row> rows;};
class Client {
    pid_t pid=-1;
    int input=-1,output=-1;
    string buffered;
    std::mutex mu;
    std::map<string,string> word_sources;
    bool broken=false;
    [[noreturn]] void transport_error(const char *message){broken=true;throw std::runtime_error(message);}
    void write(const string &message) {
        if(broken)throw std::runtime_error("engine connection closed; restart Squirrel");
        string data=message+"\n";size_t offset=0;
        auto deadline=Clock::now()+std::chrono::milliseconds(1000);
        while(offset<data.size()){
            ssize_t n=::write(input,data.data()+offset,data.size()-offset);
            if(n<0&&errno==EINTR)continue;
            if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){
                int ms=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
                if(ms<=0)transport_error("engine write timeout");
                pollfd fd{input,POLLOUT,0};int ready=poll(&fd,1,ms);
                if(ready<0&&errno==EINTR)continue;
                if(ready<=0)transport_error("engine write timeout");
                continue;
            }
            if(n<=0)transport_error("engine pipe closed");offset+=n;
        }
    }
    string line(int timeout=6000) {
        auto deadline=Clock::now()+std::chrono::milliseconds(timeout);
        for(;;){
            auto end=buffered.find('\n');
            if(end!=string::npos){string r=buffered.substr(0,end);buffered.erase(0,end+1);return r;}
            int ms=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
            if(ms<=0)transport_error("engine response timeout");
            pollfd fd{output,POLLIN,0};int ready=poll(&fd,1,ms);
            if(ready<0&&errno==EINTR)continue;
            if(ready<=0)transport_error("engine response timeout");
            char chunk[4096];ssize_t n=read(output,chunk,sizeof(chunk));
            if(n<0&&errno==EINTR)continue;
            if(n<=0)transport_error("engine exited; see engine.log");
            buffered.append(chunk,n);
            if(buffered.size()>1048576)transport_error("engine response too large");
        }
    }
    string reply(int timeout=6000){string s=line(timeout);if(s.rfind("ERROR ",0)==0)throw std::runtime_error(unhex(s.substr(6)));return s;}
public:
    explicit Client(const string &launcher) {
        int a[2],b[2];if(pipe(a))throw std::runtime_error("pipe failed");
        if(pipe(b)){close(a[0]);close(a[1]);throw std::runtime_error("pipe failed");}
        for(int fd:{a[0],a[1],b[0],b[1]})fcntl(fd,F_SETFD,FD_CLOEXEC);
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,a[0],STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions,b[1],STDOUT_FILENO);
        for(int fd:{a[0],a[1],b[0],b[1]})if(fd>2)posix_spawn_file_actions_addclose(&actions,fd);
        char *argv[]={const_cast<char *>(launcher.c_str()),nullptr};
        int rc=posix_spawn(&pid,launcher.c_str(),&actions,nullptr,argv,environ);
        posix_spawn_file_actions_destroy(&actions);close(a[0]);close(b[1]);
        input=a[1];output=b[0];
        // SIGPIPE is disabled for this descriptor only; never alter Squirrel's signal handlers.
        fcntl(input,F_SETNOSIGPIPE,1);
        fcntl(input,F_SETFL,fcntl(input,F_GETFL)|O_NONBLOCK);
        if(rc){close(input);close(output);input=output=-1;throw std::runtime_error("cannot start engine launcher");}
        try{string s=line(15000);if(s!="READY")throw std::runtime_error("engine startup failed: "+s);}
        catch(...){stop();throw;}
    }
    ~Client(){stop();}
    void stop(){
        if(input>=0){const char q[]="QUIT\n";::write(input,q,sizeof(q)-1);close(input);input=-1;}
        if(output>=0){close(output);output=-1;}
        if(pid>0){
            for(int i=0;i<50;++i){pid_t result=waitpid(pid,nullptr,WNOHANG);if(result==pid||(result<0&&errno==ECHILD)){pid=-1;break;}usleep(100000);}
            if(pid>0){kill(pid,SIGTERM);waitpid(pid,nullptr,0);pid=-1;}
        }
    }
    uint64_t open(const string &mode="quanpin",bool emoji=false){std::lock_guard<std::mutex> lock(mu);write("OPEN "+mode+(emoji?" 1":" 0"));string s=reply();uint64_t id=0;std::istringstream in(s);string tag;in>>tag>>id;if(tag!="OPENED"||!id)throw std::runtime_error("invalid session response");return id;}
    void command(const string &s){std::lock_guard<std::mutex> lock(mu);write(s);if(reply()!="OK")throw std::runtime_error("invalid engine acknowledgement");}
    void sync_words(const string &source,const std::vector<std::pair<string,string>> &entries){
        if(source.empty())return;
        if(entries.size()>2048||source.size()>4096)throw std::runtime_error("oversized user-word configuration");
        size_t bytes=0;
        for(const auto &entry:entries){
            bytes+=entry.first.size()+entry.second.size();
            if(entry.first.size()>128||entry.second.size()>4096||bytes>2*1024*1024)
                throw std::runtime_error("oversized user-word configuration");
        }
        string request="WORD_SYNC "+hex(source)+" "+std::to_string(entries.size());
        for(const auto &entry:entries)request+="\nW "+hex(entry.first)+" "+hex(entry.second);
        request+="\nEND_WORDS";
        std::lock_guard<std::mutex> lock(mu);
        auto old=word_sources.find(source);
        if(old!=word_sources.end()&&old->second==request)return;
        write(request);if(reply(30000)!="OK")throw std::runtime_error("invalid user-word acknowledgement");
        word_sources[source]=std::move(request);
    }
    Result query(uint64_t id,const string &text,const string &before={}){
        std::lock_guard<std::mutex> lock(mu);write("QUERY "+std::to_string(id)+" "+hex(text)+" "+hex(before));
        Result result{};size_t count=0;string tag;std::istringstream in(reply());in>>tag>>result.ticket>>count;
        if(tag!="RESULT"||count>64||!result.ticket)transport_error("invalid candidate response");
        // Consume one complete frame before validating its candidate fields.
        // A bad row must never leave C/END lines to poison the next request.
        std::vector<string> rows;
        for(size_t i=0;i<count;++i)rows.push_back(line());
        if(line()!="END")transport_error("incomplete candidate response");
        for(size_t i=0;i<count;++i){
            Row row{};string payload,preedit;std::istringstream r(rows[i]);r>>tag>>row.index>>row.cover>>payload>>preedit;
            if(tag!="C"||row.index!=i||!row.cover||row.cover>text.size())throw std::runtime_error("invalid candidate coverage");
            if(!r)throw std::runtime_error("invalid candidate fields");
            row.text=unhex(payload);row.preedit=unhex(preedit);result.rows.push_back(std::move(row));
        }
        return result;
    }
};
}

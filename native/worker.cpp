// Long-lived native engine host. Only stdin/stdout pipes cross the sandbox.
#include <dlfcn.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <execinfo.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "abi_macos_3.h"
#include "text_context_abi.h"
#include <wetype_modes.h>
using namespace std::chrono_literals;
using std::string;
using wetype::InputMode;
using wetype::input_mode;
static string worker_group="pinyin";
struct DictItem { void *fd; const char *path; uint32_t id, version; };
struct DictConfig { DictItem *items; uint32_t count; const char *user_dict; uint8_t reserved; };
struct RawCandidate {
    const char *text; uint32_t text_len;
    const char *comment; uint32_t comment_len;
    const void *id; uint32_t id_len;
    unsigned char tail[68];
};
static_assert(sizeof(RawCandidate) == 112);
struct Candidate { string text, id; uint32_t cover; };
struct PendingInput {
    string input,display;
    std::vector<size_t> ends{0};
    string prefix(size_t cover) const {return cover<ends.size()?display.substr(0,ends[cover]):string();}
};
static PendingInput pending_input(const void *data) {
    PendingInput out;
    auto node=static_cast<const char *>(data);
    // Mac 3.0.1 pending-input event (type 5) owns a linked list for the
    // duration of the callback: type at 0, next at 8, payload at 24.
    // Type 1 carries exact-match text; 0 is already selected text, rendered
    // separately by Rime. Type 6 is a syllable separator
    // (NUL for an automatic boundary, apostrophe for a typed delimiter).
    for(size_t count=0;node;++count){
        if(count>=258)return {};
        uint32_t type;const char *next;
        memcpy(&type,node,4);memcpy(&next,node+8,8);
        string text;
        if(type==1){
            uint32_t length;const char *value;
            memcpy(&length,node+24,4);memcpy(&value,node+32,8);
            if(length>128||(!value&&length))return {};
            if(length)text.assign(value,length);
        }else if(type==6){
            char delimiter;memcpy(&delimiter,node+24,1);
            if(delimiter=='\0'){
                out.display+='\'';out.ends.back()=out.display.size();
            }else if(delimiter=='\'')text="'";
            else return {};
        }else if(type!=0&&type!=7)return {}; // selected text and cursor are rendered by Rime
        for(char ch:text){
            if((ch<'a'||ch>'z')&&(ch<'A'||ch>'Z')&&ch!='\''&&ch!=';')return {};
            out.input+=ch;out.display+=ch;out.ends.push_back(out.display.size());
        }
        if(out.input.size()>128||out.display.size()>256)return {};
        node=next;
    }
    return out;
}
struct Api {
    void (*initialize)(const WetypeConfig *);
    bool (*config_dict)(const DictConfig *);
    uint64_t (*create)(const void *);
    void (*destroy)(uint64_t);
    void (*listen)(uint64_t, void *, uint64_t);
    int (*input)(uint64_t, const char *, uint32_t, const void *, uint32_t);
    int (*drop)(uint64_t,uint8_t,const void *,uint32_t);
    void (*set_context)(uint64_t,const TextAroundCursor *,uint32_t);
#ifdef WETYPE_CONTEXT_TEST
    void (*context_snapshot)(uint64_t,void (*)(const TextAroundCursor *,void *),void *);
#endif
    bool (*next)(uint64_t, RawCandidate *);
    void (*erase_iterator)(uint64_t);
    void (*select)(uint64_t,const char *,uint32_t,const void *,uint32_t,
                   const char *,uint32_t,const char *,uint32_t,void *,uint32_t,const char *const *);
    void (*finalize)();
} api;
template<class T> static void symbol(void *image, T &out, const char *name) {
    out = reinterpret_cast<T>(dlsym(image,name));
    if (!out) throw std::runtime_error(string("missing engine symbol: ")+name);
}
static void rotate_log() {
    const char *path=getenv("WETYPE_ENGINE_LOG");
    struct stat st{};
    if(!path||fstat(STDERR_FILENO,&st)||!S_ISREG(st.st_mode)||st.st_size<8*1024*1024)return;
    string previous=string(path)+".previous.log";
    if(rename(path,previous.c_str()))return;
    int fd=open(path,O_WRONLY|O_CREAT|O_APPEND,0600);
    if(fd>=0){fflush(nullptr);dup2(fd,STDERR_FILENO);dup2(fd,STDOUT_FILENO);close(fd);}
}
static FILE *protocol;
static int crash_fd = -1;
static void crash_handler(int sig, siginfo_t *info, void *) {
    void *stack[48]; int n = backtrace(stack,48);
    dprintf(crash_fd,"signal=%d address=%p\n",sig,info->si_addr);
    backtrace_symbols_fd(stack,n,crash_fd); _exit(128+sig);
}
static void log_message(int level,const char *s) { if(s) fprintf(stderr,"[engine:%d] %s\n",level,s); }
static string hex(const string &s) {
    static const char *digits="0123456789abcdef"; string r;
    for(unsigned char c:s) { r+=digits[c>>4]; r+=digits[c&15]; } return r.empty()?"-":r;
}
static string unhex(const string &s) {
    if(s=="-") return {};
    if(s.size()%2 || s.size()>8192) throw std::runtime_error("invalid payload");
    string r;
    auto nib=[](char c)->int { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; throw std::runtime_error("invalid hex"); };
    for(size_t i=0;i<s.size();i+=2)r+=char((nib(s[i])<<4)|nib(s[i+1])); return r;
}
struct Session {
    const InputMode *mode=&input_mode("quanpin");
    uint64_t sid=0,generation=0,revision=0,ticket=0,completed=0;
    bool closed=false;
    uint32_t native_version=0;
    bool version_known=false;
    string input, issued_input,before;
    bool context_applied=false;
    PendingInput pending,issued_pending;
    std::vector<Candidate> candidates,issued;
    std::mutex mu;
    std::condition_variable changed;
};
struct Listener { Session *session; uint64_t generation; };
static std::vector<std::unique_ptr<Listener>> listeners;
static std::map<uint64_t,std::unique_ptr<Session>> sessions;
static uint64_t next_handle=1;
static uint64_t prepared_handle=0;
static void events(uint64_t,const uint32_t *types,void *const *data,uint32_t count,uint64_t context) {
    auto &l=*reinterpret_cast<Listener *>(context);
    if(!types||!data||count>128)return;
    for(uint32_t i=0;i<count;++i) {
        if(types[i]==5&&data[i]){
            auto pending=pending_input(data[i]);
            std::lock_guard<std::mutex> lock(l.session->mu);
            if(!l.session->closed&&l.session->generation==l.generation)l.session->pending=std::move(pending);
        }
        if(types[i]!=0||!data[i])continue;
        uint64_t it; memcpy(&it,data[i],8);
        // Mac 3.0.1 candidate-list event: local iterator at 0, version at 24.
        // The version advances per input mutation, including coalesced keys.
        uint32_t version;memcpy(&version,static_cast<char *>(data[i])+24,4);
        std::vector<Candidate> rows;
        RawCandidate raw{};
        while(it && rows.size()<64 && api.next(it,&raw)) {
            uint32_t cover; memcpy(&cover,reinterpret_cast<char *>(&raw)+100,4);
            if(!raw.text||raw.text_len>4095||!raw.id||raw.id_len>4096||!cover)break;
            rows.push_back({string(raw.text,raw.text_len),string(static_cast<const char *>(raw.id),raw.id_len),cover});
        }
        if(it)api.erase_iterator(it);
        auto &s=*l.session;
        { std::lock_guard<std::mutex> lock(s.mu);
          if(s.closed||s.generation!=l.generation)continue;
          if(s.version_known&&static_cast<int32_t>(version-s.native_version)<0)continue;
          s.native_version=version;s.version_known=true;
          s.candidates=std::move(rows); ++s.revision; }
        s.changed.notify_all();
    }
    {std::lock_guard<std::mutex> lock(l.session->mu);
     if(!l.session->closed&&l.session->generation==l.generation)++l.session->completed;}
    l.session->changed.notify_all();
}
static void reset(Session &s) {
    uint64_t previous;
    { std::lock_guard<std::mutex> lock(s.mu);
      previous=s.sid; ++s.generation; s.input.clear();s.issued.clear();s.candidates.clear();s.pending={};s.issued_pending={};s.version_known=false;s.before.clear();s.context_applied=false;++s.ticket; }
    if(previous)api.destroy(previous);
    alignas(8) unsigned char cfg[312]={};
    // The dedicated host already serializes requests. Use the native session
    // directly, without a second asynchronous input queue inside the engine.
    cfg[1]=1; cfg[9]=cfg[10]=cfg[11]=cfg[12]=1;
    cfg[47]=cfg[58]=cfg[60]=cfg[174]=cfg[248]=cfg[308]=1;
    memcpy(cfg+4,&s.mode->keyboard,4);memcpy(cfg+32,&s.mode->double_pin,4);
    if(s.mode->keyboard==5)cfg[41]=1; // candidates on; native auto-commit remains off
    const char *app="WetypeRime";uint32_t len=10;
    memcpy(cfg+192,&app,8);memcpy(cfg+220,&len,4);
    s.sid=api.create(cfg);
    if(!s.sid)throw std::runtime_error("engine session creation failed");
    auto l=std::make_unique<Listener>(Listener{&s,s.generation});
    api.listen(s.sid,reinterpret_cast<void *>(events),reinterpret_cast<uint64_t>(l.get()));
    listeners.push_back(std::move(l));
}
struct Barrier{uint64_t revision,completed;uint32_t version;bool known;};
static Barrier barrier(Session &s){
    std::lock_guard<std::mutex> lock(s.mu);
    return {s.revision,s.completed,s.native_version,s.version_known};
}
static void wait_batch(Session &s,const Barrier &before,uint32_t mutations=1) {
    std::unique_lock<std::mutex> lock(s.mu);
    if(!s.changed.wait_for(lock,1800ms,[&]{
        return s.revision>before.revision&&s.completed>before.completed&&
               (!before.known||static_cast<int32_t>(s.native_version-(before.version+mutations))>=0);
    }))
        throw std::runtime_error("candidate timeout");
}
static void query(Session &s,const string &text,const string &before) {
    if(text.empty()||text.size()>128||text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';")!=string::npos||
       (!s.mode->semicolon&&text.find(';')!=string::npos))
        throw std::runtime_error("invalid input");
    if(before.size()>1024||before.find('\0')!=string::npos)throw std::runtime_error("invalid context");
    if(s.context_applied&&s.before!=before)reset(s);
    if(text.size()<s.input.size()&&s.input.compare(0,text.size(),text)==0){
        auto before=barrier(s);
        // Native C API takes the deletion count. Preserve the current session
        // and partial-selection history instead of replaying a long prefix.
        api.drop(s.sid,static_cast<uint8_t>(s.input.size()-text.size()),nullptr,0);
        wait_batch(s,before);s.input=text;
    }else if(text.compare(0,s.input.size(),s.input)!=0)reset(s);
    if(!s.context_applied){
        TextAroundCursor context{};
        context.before=before.data();context.before_len=static_cast<uint32_t>(before.size());
        context.after=context.on=context.clipboard=context.extra="";
        api.set_context(s.sid,&context,0);
        s.before=before;s.context_applied=true;
    }
    size_t start=s.input.size();
    auto key=[&](char ch){
        char key[4]={'d',' ',ch,0};
        api.input(s.sid,key,3,nullptr,0);key[0]='u';api.input(s.sid,key,3,nullptr,0);
    };
    // A new native session starts at an opaque version. Learn it from one
    // acknowledged key, then let the engine coalesce the remaining key batch.
    if(start<text.size()&&!barrier(s).known){
        auto before=barrier(s);key(text[start]);wait_batch(s,before);s.input+=text[start++];
    }
    if(start<text.size()){
        auto before=barrier(s);
        for(size_t i=start;i<text.size();++i)key(text[i]);
        wait_batch(s,before,static_cast<uint32_t>(text.size()-start));
        s.input=text;
    }
    std::lock_guard<std::mutex> lock(s.mu);
    if(s.issued_input!=s.input||s.issued.empty()){
        s.issued.clear();
        for(auto row:s.candidates){
            // The native ABI uses UINT32_MAX for the entire current input,
            // including English literals and incomplete/corrected syllables.
            if(row.cover==UINT32_MAX)row.cover=static_cast<uint32_t>(s.input.size());
            if(row.cover&&row.cover<=s.input.size())s.issued.push_back(std::move(row));
        }
        // Ignore unsupported/stale pending structures rather than displaying
        // another input revision or changing the user's actual spelling.
        s.issued_pending=s.pending.input==s.input?s.pending:PendingInput{};
        s.issued_input=s.input;++s.ticket;
    }
    fprintf(protocol,"RESULT %llu %zu\n",(unsigned long long)s.ticket,s.issued.size());
    for(size_t i=0;i<s.issued.size();++i)
        fprintf(protocol,"C %zu %u %s %s\n",i,s.issued[i].cover,hex(s.issued[i].text).c_str(),
                hex(s.issued_pending.prefix(s.issued[i].cover)).c_str());
    fprintf(protocol,"END\n");fflush(protocol);
}
static void select(Session &s,uint64_t ticket,size_t index) {
    Candidate row;auto before=barrier(s);
    {std::lock_guard<std::mutex> lock(s.mu);
      if(ticket!=s.ticket||index>=s.issued.size()||s.issued_input!=s.input)throw std::runtime_error("stale candidate");
      row=s.issued[index];
      if(row.cover>s.input.size())throw std::runtime_error("invalid candidate coverage");
      s.issued.clear();++s.ticket; }
    api.select(s.sid,row.text.data(),(uint32_t)row.text.size(),row.id.data(),(uint32_t)row.id.size(),nullptr,0,nullptr,0,nullptr,0,nullptr);
    s.input.erase(0,row.cover);
    if(!s.input.empty())wait_batch(s,before);
    else {
        std::unique_lock<std::mutex> lock(s.mu);
        if(!s.changed.wait_for(lock,1800ms,[&]{return s.completed>before.completed;}))
            throw std::runtime_error("selection acknowledgement timeout");
    }
    fprintf(protocol,"OK\n");fflush(protocol);
}
static void start(const char *engine,const char *work,const char *table) {
    int sock=socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(9);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int rc=connect(sock,reinterpret_cast<sockaddr *>(&addr),sizeof(addr));int error=errno;close(sock);
    if(rc!=-1||error!=EPERM)throw std::runtime_error("network isolation missing");
    void *image=dlopen(engine,RTLD_NOW|RTLD_LOCAL);
    if(!image)throw std::runtime_error(dlerror());
    symbol(image,api.initialize,"wxime_initialize");symbol(image,api.config_dict,"wxime_config_dict");
    symbol(image,api.create,"wxime_create_session");symbol(image,api.destroy,"wxime_destroy_session");
    symbol(image,api.listen,"wxime_add_session_listener");symbol(image,api.input,"wxime_process_input");
    symbol(image,api.drop,"wxime_drop_last");
    symbol(image,api.set_context,"wxime_set_text_around_cursor_struct");
#ifdef WETYPE_CONTEXT_TEST
    symbol(image,api.context_snapshot,"wxime_get_text_around_cursor_snap_shot");
#endif
    symbol(image,api.next,"wxime_candidate_next");symbol(image,api.erase_iterator,"wxime_delete_candidate_iterator");
    symbol(image,api.select,"wxime_select_candidate");symbol(image,api.finalize,"wxime_finalize");
    if(const char *path=getenv("WETYPE_CRASH_LOG")) {
        crash_fd=open(path,O_WRONLY|O_CREAT|O_APPEND,0600);
        if(crash_fd<0)throw std::runtime_error("cannot open crash.log");
        struct sigaction a{};a.sa_sigaction=crash_handler;a.sa_flags=SA_SIGINFO;
        for(int sig:{SIGSEGV,SIGABRT,SIGBUS,SIGILL})sigaction(sig,&a,nullptr);
    }
    WetypeConfig cfg{};cfg.logger=reinterpret_cast<void *>(log_message);
    cfg.logger_config_json="{\"logger\":{\"appender\":{\"type\":\"capi\"}},\"channels\":[]}";
    cfg.work_dir=work;cfg.syllable_prefix_file_name="";cfg.uin="10000001";cfg.uin_len=8;
    cfg.log_level=5;cfg.platform=5;cfg.device_performance=-5;
    cfg.app_ver="3.0.1";cfg.app_ver_len=5;cfg.device_name="WetypeRime";cfg.device_name_len=10;
    cfg.system_version="macOS";cfg.system_version_len=5;cfg.cpu_score=cfg.gpu_score=1;cfg.package_type=3;
    api.initialize(&cfg);
    static std::vector<string> paths;
    static std::vector<DictItem> items;
    FILE *f=fopen(table,"r");if(!f)throw std::runtime_error("cannot open dictionary table");
    char path[4096];uint32_t id,version;
    paths.reserve(64);items.reserve(64);
    while(fscanf(f,"%4095[^\t]\t%u\t%u\n",path,&id,&version)==3) {
        paths.emplace_back(path);items.push_back({nullptr,paths.back().c_str(),id,version});
        if(items.size()>64)throw std::runtime_error("too many dictionaries");
    }
    fclose(f);static string user=string(work)+"/userdict";
    DictConfig dict{items.data(),static_cast<uint32_t>(items.size()),user.c_str(),0};
    if(items.empty()||!api.config_dict(&dict))throw std::runtime_error("dictionary configuration failed");
    // Prepare the first native session while the frontend's background loader
    // is running. Never synthesize input or selections to warm the engine.
    auto initial=std::make_unique<Session>();
    for(const auto &mode:wetype::input_modes)if(worker_group==mode.group){initial->mode=&mode;break;}
    if(worker_group!=initial->mode->group)throw std::runtime_error("unknown dictionary group");
    reset(*initial);
    prepared_handle=next_handle++;sessions[prepared_handle]=std::move(initial);
}
int main(int argc,char **argv) {
    if(argc!=4&&argc!=5)return 2;
    if(argc==5)worker_group=argv[4];
    // Protect shared learning data from simultaneous frontend/helper processes.
    string lockpath=string(argv[2])+"/engine.lock";
    int lock=open(lockpath.c_str(),O_CREAT|O_RDWR,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 3;
    protocol=fdopen(dup(STDOUT_FILENO),"w");dup2(STDERR_FILENO,STDOUT_FILENO);
    rotate_log();
    try { start(argv[1],argv[2],argv[3]);fprintf(protocol,"READY\n");fflush(protocol); }
    catch(const std::exception &e){fprintf(protocol,"ERROR %s\n",hex(e.what()).c_str());fflush(protocol);_exit(1);}
    char *line=nullptr;size_t cap=0;
    while(getline(&line,&cap,stdin)>0) {
        uint64_t handle=0;
        try {
            rotate_log();
            if(strlen(line)>16384)throw std::runtime_error("request too long");
            std::istringstream in(line);string cmd,arg;uint64_t ticket=0;size_t index=0;
            in>>cmd;
            if(cmd=="QUIT")break;
            if(cmd=="OPEN") {
                if(!(in>>arg))arg=worker_group=="pinyin"?"quanpin":worker_group;
                const auto &mode=input_mode(arg);
                if(worker_group!=mode.group)throw std::runtime_error("input mode dictionary mismatch");
                uint64_t id=prepared_handle;
                if(id){prepared_handle=0;auto &s=*sessions[id];if(s.mode!=&mode){s.mode=&mode;reset(s);}}
                else{auto s=std::make_unique<Session>();s->mode=&mode;reset(*s);id=next_handle++;sessions[id]=std::move(s);}
                fprintf(protocol,"OPENED %llu\n",(unsigned long long)id);fflush(protocol);continue;
            }
            if(!(in>>handle)||!sessions.count(handle)||sessions[handle]->closed)throw std::runtime_error("unknown session");
            auto &s=*sessions[handle];
            if(cmd=="QUERY" && in>>arg){string before;in>>before;query(s,unhex(arg),before.empty()?string():unhex(before));continue;}
#ifdef WETYPE_CONTEXT_TEST
            if(cmd=="CONTEXT_STATUS"){
                struct Snapshot{bool received=false,matched=false;size_t length=0;string expected;};
                Snapshot snapshot{};snapshot.expected=s.before;
                api.context_snapshot(s.sid,[](const TextAroundCursor *c,void *p){
                    auto &out=*static_cast<Snapshot *>(p);out.received=true;out.length=c->before_len;
                    out.matched=c->before_len==out.expected.size()&&(!c->before_len||
                        (c->before&&std::memcmp(c->before,out.expected.data(),c->before_len)==0));
                },&snapshot);
                fprintf(protocol,"CONTEXT %d %d %zu\n",snapshot.received,snapshot.matched,snapshot.length);fflush(protocol);continue;
            }
#endif
            if(cmd=="SELECT" && in>>ticket>>index){select(s,ticket,index);continue;}
            if(cmd=="RESET")reset(s);
            else if(cmd=="CLOSE") {
                {std::lock_guard<std::mutex> guard(s.mu);s.closed=true;s.issued.clear();s.candidates.clear();}
                api.destroy(s.sid);s.sid=0;
            } else throw std::runtime_error("invalid command");
            fprintf(protocol,"OK\n");fflush(protocol);
        }catch(const std::exception &e){
            // A timed-out operation may have changed vendor state. Recreate
            // that session before accepting more requests; never replay a select.
            if(sessions.count(handle)&&!sessions[handle]->closed)try{reset(*sessions[handle]);}catch(...){}
            fprintf(stderr,"[wetype] %s\n",e.what());fprintf(protocol,"ERROR %s\n",hex(e.what()).c_str());fflush(protocol);
        }
    }
    for(auto &p:sessions)if(p.second->sid)api.destroy(p.second->sid);
    api.finalize();fflush(nullptr);_exit(0);
}

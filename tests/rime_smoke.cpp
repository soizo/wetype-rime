#include "../src/rime_text_context.h"
// Exercise the installed librime and its actual native component path.
#include <rime_api.h>
#include <rime/context.h>
#include <rime/key_event.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <wetype_modes.h>
#include "imk_fixture.h"
static RimeApi *api;
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static std::string property(RimeSessionId id,const char *name){
    char value[512]={};api->get_property(id,name,value,sizeof(value));return value;
}
static void type(RimeSessionId id,const std::string &s){for(unsigned char c:s){
    auto started=std::chrono::steady_clock::now();
    require(api->process_key(id,c,(c>='A'&&c<='Z')?1:0),"input not handled");
    // IMK asks for the menu after every key, including incomplete prefixes.
    RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"no per-key context");
    api->free_context(&ctx);
    require(property(id,"_wetype_error").empty(),"engine failed during per-key rendering");
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
    printf("key-render-ms=%lld\n",static_cast<long long>(ms));
}}
static std::vector<std::string> menu(RimeSessionId id){
    RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"no Rime context");
    std::vector<std::string> rows;
    printf("preedit=%s page=%d\n",ctx.composition.preedit?ctx.composition.preedit:"",ctx.menu.page_no);
    for(int i=0;i<ctx.menu.num_candidates;++i){rows.emplace_back(ctx.menu.candidates[i].text);printf("%d. %s\n",i,rows.back().c_str());}
    api->free_context(&ctx);return rows;
}
static int find(const std::vector<std::string> &rows,const std::string &text){for(size_t i=0;i<rows.size();++i)if(rows[i]==text)return i;return -1;}
static std::string commit(RimeSessionId id){RIME_STRUCT(RimeCommit,out);if(!api->get_commit(id,&out))return {};std::string s=out.text;api->free_commit(&out);printf("commit=%s\n",s.c_str());return s;}
static void expect_preedit(RimeSessionId id,const std::string &expected){
    RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"preedit context unavailable");
    std::string actual=ctx.composition.preedit?ctx.composition.preedit:"";
    api->free_context(&ctx);
    printf("preedit-check=%s\n",actual.c_str());
    require(actual==expected,"engine syllable boundaries missing or stale in Rime preedit");
}
static void expect_spelling(RimeSessionId id,const std::string &expected){
    require(expected==api->get_input(id),"input capitalization changed");
    RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"case context unavailable");
    std::string actual=ctx.composition.preedit?ctx.composition.preedit:"",raw=expected;
    api->free_context(&ctx);
    actual.erase(std::remove(actual.begin(),actual.end(),'\''),actual.end());
    raw.erase(std::remove(raw.begin(),raw.end(),'\''),raw.end());
    require(actual==raw,"preedit capitalization changed");
}
static std::string mode_sample(const std::string &mode){
    if(mode=="quanpin")return "nihao";
    if(mode=="flypy")return "nihc";
    if(mode=="ziguang")return "nihq";
    if(mode=="jiajia")return "nihd";
    if(mode.rfind("wubi",0)==0)return "wqvb";
    return "nihk";
}
static void select_mode(RimeSessionId id,const std::string &mode){
    for(const auto &option:wetype::input_modes)api->set_option(id,(std::string("wetype_mode_")+option.id).c_str(),false);
    api->set_option(id,("wetype_mode_"+mode).c_str(),true);
}
static void semicolon_punctuation(RimeSessionId id, const char *code) {
    auto with_punctuation = [&](char key) {
        type(id, code);
        require(api->process_key(id, key, 0), "punctuation key not handled");
        require(std::string(api->get_input(id)).empty(), "punctuation left a composition open");
        return commit(id);
    };
    auto baseline = with_punctuation(',');
    const std::string comma = "，";
    require(baseline.size() >= comma.size() && baseline.substr(baseline.size() - comma.size()) == comma,
            "comma did not commit with the preceding input");
    auto actual = with_punctuation(';');
    require(actual == baseline.substr(0, baseline.size() - comma.size()) + "；",
            "semicolon did not commit like ordinary punctuation");
    require(property(id, "_wetype_error").empty(), "punctuation was sent to the native engine as spelling");
}
static std::vector<std::string> all_menu(RimeSessionId id){
    std::vector<std::string> rows;RimeCandidateListIterator it{};
    if(api->candidate_list_begin(id,&it)){
        while(api->candidate_list_next(&it)){
            std::string text=it.candidate.text;
            for(const char *hidden:{"联想","独立","精准音","模糊音","颜文字","符号表情","拼音辅助","纯五笔","隐码","显码"})
                require(text.find(hidden)==std::string::npos,"configuration preference is still exposed in the options menu");
            rows.push_back(text);
        }
        api->candidate_list_end(&it);
    }
    return rows;
}
static void menu_mode(RimeSessionId id,const std::string &name){
    std::string label=name;
    require(api->process_key(id,0xffc1,0),"Rime options menu did not open");
    auto rows=all_menu(id);int pos=find(rows,label);
    if(pos<0){
        // The official default menu may initially fold all option groups.
        int folded=-1;
        for(size_t i=0;i<rows.size();++i){printf("switch-menu=%zu %s\n",i,rows[i].c_str());if(rows[i].find('/')!=std::string::npos||rows[i].find("／")!=std::string::npos)folded=i;}
        require(folded>=0,"folded Rime options unavailable");
        require(api->select_candidate(id,folded),"Rime options did not unfold");
        rows=all_menu(id);pos=find(rows,label);
    }
    require(pos>=0,"input mode missing from actual Rime options menu");
    require(api->select_candidate(id,pos),"Rime menu option selection failed");
}
static void input_modes(RimeSessionId id){
    for(bool standalone:{false,true})for(const auto &mode:wetype::input_modes){
        std::string schema=standalone?std::string("wetype_")+mode.id:"wetype";
        require(api->select_schema(id,schema.c_str()),"native mode schema unavailable");
        api->set_option(id,"ascii_mode",false);
        if(!standalone)select_mode(id,mode.id);
        std::string sample=mode_sample(mode.id);
        printf("MODE %s %s\n",schema.c_str(),mode.id);
        type(id,sample);expect_spelling(id,sample);
        require(property(id,"_wetype_mode")==mode.id,"wrong native input mode");
        require(property(id,"_wetype_error").empty(),"mode engine error");
        auto rows=menu(id);int pos=find(rows,"你好");require(pos>=0,"mode did not decode its own keyboard layout");
        require(api->select_candidate(id,pos),"mode candidate selection failed");
        require(commit(id)=="你好","mode commit failed");
        type(id,sample);require(api->process_key(id,0xff08,0),"mode deletion failed");
        expect_spelling(id,sample.substr(0,sample.size()-1));
        type(id,sample.substr(sample.size()-1));
        require(find(menu(id),"你好")>=0,"mode delete/retype failed");
        api->clear_composition(id);
        if(mode.keyboard==5){
            api->set_option(id,"wetype_wubi_pinyin",false);
            type(id,"wqvbwqvb");std::string text=commit(id);
            require(api->process_key(id,' ',0),"continuous Wubi space failed");text+=commit(id);
            require(text=="你好你好","continuous four-code Wubi failed");
            api->set_option(id,"wetype_wubi_pinyin",true);
        }
        if(mode.semicolon){
            type(id,"j;");expect_spelling(id,"j;");
            require(!menu(id).empty()&&property(id,"_wetype_error").empty(),"double pinyin semicolon failed");
            api->clear_composition(id);
            semicolon_punctuation(id, "ni");
        } else if (std::string(mode.id) == "flypy") {
            semicolon_punctuation(id, "go");
        }
    }
    require(api->select_schema(id,"wetype"),"cannot restore unified wetype");
    select_mode(id,"quanpin");api->set_option(id,"ascii_mode",false);
    type(id,"nihao");
    select_mode(id,"flypy");require(std::string(api->get_input(id)).empty(),"mode change retained old composition");
    type(id,"nihc");require(find(menu(id),"你好")>=0,"live mode change failed");api->clear_composition(id);
    select_mode(id,"quanpin");
    require(api->process_key(id,';',0),"ordinary semicolon not handled");
    require(commit(id)=="；","full pinyin swallowed semicolon as a spelling key");
    menu_mode(id,"小鹤双拼");
    type(id,"nihc");require(property(id,"_wetype_mode")=="flypy"&&find(menu(id),"你好")>=0,"actual menu option did not change native mode");
    api->clear_composition(id);
    require(api->select_schema(id,"wetype_quanpin")&&api->select_schema(id,"wetype"),"saved mode schema return failed");
    api->set_option(id,"ascii_mode",false);type(id,"nihc");
    require(property(id,"_wetype_mode")=="flypy"&&find(menu(id),"你好")>=0,"unified mode option was not saved");
    api->clear_composition(id);menu_mode(id,"全拼");
    puts("PASS: unified options and all 11 standalone engine modes, decoding, selection, deletion, semicolon and live switching");
}
static void surrounding_context(RimeSessionId id){
    require(api->select_schema(id,"wetype"),"context test schema unavailable");
    select_mode(id,"quanpin");
    api->set_option(id,"ascii_mode",false);
    struct Case {RimeSessionId id;const char *source;size_t bytes,reads,after=0,on=0;};
    auto check=[](void *value){
        auto &test=*static_cast<Case *>(value);
        type(test.id,"nihao");
        require(property(test.id,"_wetype_context_source")==test.source,"wrong surrounding-text source");
        require(property(test.id,"_wetype_context_bytes")==std::to_string(test.bytes),"surrounding prefix missing or includes marked text");
        require(property(test.id,"_wetype_context_after_bytes")==std::to_string(test.after),"surrounding suffix missing or includes marked text");
        require(property(test.id,"_wetype_context_on_bytes")==std::to_string(test.on),"selection missing or marked text leaked");
        require(find(menu(test.id),"你好")>=0,"context query broke native candidates");
        require(api->process_key(test.id,0xff08,0),"context deletion failed");type(test.id,"o");
        require(imk_fixture_reads()==test.reads,"surrounding text was repeatedly fetched per key");
    };
    Case chinese{id,"imk",10,2,6};
    imk_fixture_perform("前文😀ni'hao后文",6,4,6,false,check,&chinese);
    api->clear_composition(id);
    Case another{id,"imk",9,2};
    imk_fixture_perform("另一个",3,size_t(-1),0,false,check,&another);
    api->clear_composition(id);
    Case selection{id,"imk",3,3,3,7};
    imk_fixture_perform("前选😀后",1,size_t(-1),0,false,check,&selection,3);
    api->clear_composition(id);
    std::string suffix(300,'y');Case suffix_case{id,"imk",0,1,256};
    imk_fixture_perform(suffix.c_str(),0,size_t(-1),0,false,check,&suffix_case);
    api->clear_composition(id);
    Case oversized{id,"imk",0,1};
    imk_fixture_perform(suffix.c_str(),0,size_t(-1),0,false,check,&oversized,300);
    api->clear_composition(id);
    Case missing{id,"unavailable",0,0};
    imk_fixture_perform("不提供上下文",0,size_t(-1),0,true,check,&missing);
    api->clear_composition(id);
    Case empty{id,"imk",0,1};
    imk_fixture_perform("",0,size_t(-1),0,false,check,&empty);
    api->clear_composition(id);
    std::string long_text(300,'x');Case bounded{id,"imk",256,2};
    imk_fixture_perform(long_text.c_str(),300,size_t(-1),0,false,check,&bounded);
    api->clear_composition(id);
    require(api->select_schema(id,"wetype_quanpin"),"independent context schema unavailable");
    api->set_option(id,"ascii_mode",false);
    Case disabled{id,"disabled",0,0};
    imk_fixture_perform("不应读取",4,size_t(-1),0,false,check,&disabled);
    api->clear_composition(id);
    require(api->select_schema(id,"wetype"),"cannot restore context-enabled schema");
    select_mode(id,"quanpin");api->set_option(id,"ascii_mode",false);
    // Outside an input event, no previous client may remain visible.
    type(id,"nihao");require(property(id,"_wetype_context_source")=="unavailable","IMK client leaked outside its event");
    api->clear_composition(id);
    puts("PASS: IMK prefix, marked-text exclusion, emoji, bounded reads, unavailable clients, file-configured opt-out and event lifetime");
}
static void timing(RimeSessionId id){
    using Clock=std::chrono::steady_clock;
    using Ms=std::chrono::milliseconds;
    const std::string sample="nihaozhongguowoxihuanzhongwenshuru";
    for(auto mode:{std::pair<int,int>{1,20},{1,10},{1,5},{8,0}}){
        int batch=mode.first,interval=mode.second;
        api->clear_composition(id);
        std::string expected;
        for(bool deleting:{false,true}){
            std::vector<double> duration;
            double max_lag=0,tail=0;
            auto target=Clock::now();
            for(size_t index=0;index<sample.size();++index){
                if(batch==1)std::this_thread::sleep_until(target);
                auto started=Clock::now();
                if(batch==1)max_lag=std::max(max_lag,std::chrono::duration<double,std::milli>(started-target).count());
                int key=deleting?0xff08:sample[index];
                require(api->process_key(id,key,0),"timed input not handled");
                if(deleting)expected.pop_back();else expected+=sample[index];
                if((index+1)%batch==0||index+1==sample.size()){
                    RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"timed context unavailable");
                    require(property(id,"_wetype_error").empty(),"engine failed in timed burst");
                    require(expected==api->get_input(id),"timed burst lost or reordered input");
                    std::string displayed=ctx.composition.preedit?ctx.composition.preedit:"";
                    displayed.erase(std::remove(displayed.begin(),displayed.end(),'\''),displayed.end());
                    require(displayed==expected,"timed preedit lost or retained letters");
                    api->free_context(&ctx);
                }
                auto ended=Clock::now();duration.push_back(std::chrono::duration<double,std::milli>(ended-started).count());
                target+=Ms(interval);
                if(batch==1)tail=std::max(0.0,std::chrono::duration<double,std::milli>(ended-target).count());
            }
            std::sort(duration.begin(),duration.end());
            printf("TIMING %s batch=%d interval=%dms keys=%zu median=%.1fms p95=%.1fms max-start-lag=%.1fms tail=%.1fms\n",
                   deleting?"delete":"typing",batch,interval,duration.size(),duration[duration.size()/2],duration[duration.size()*95/100],max_lag,tail);
        }
    }
    // A whole burst can arrive before the frontend asks for any candidates.
    // Selection must still refer to the final input, never an obsolete prefix.
    for(char key:std::string("nihaozhongguo"))require(api->process_key(id,key,0),"bulk input not handled");
    require(menu(id)[0]=="你好中国","bulk menu is stale");
    require(api->process_key(id,' ',0),"bulk space not handled");
    require(commit(id)=="你好中国","bulk selection committed an obsolete prefix");
}
int main(int argc,char **argv){
    if(argc!=3&&argc!=5&&argc!=6)return 2;
    setvbuf(stdout,nullptr,_IONBF,0);
    signal(SIGSEGV,[](int sig){void *b[40];int n=backtrace(b,40);backtrace_symbols_fd(b,n,2);_exit(128+sig);});
    try{
        imk_fixture_install();
        void *lua=dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL);
        require(lua,dlerror());
        api=rime_get_api();
        const char *modules[]={"default","lua",nullptr};
        RIME_STRUCT(RimeTraits,traits);
        traits.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";
        traits.user_data_dir=argv[1];traits.app_name="rime.wetype-test";traits.modules=modules;
        traits.log_dir=argv[2];traits.min_log_level=2;
        api->setup(&traits);api->initialize(&traits);
        api->start_maintenance(true);api->join_maintenance_thread();
        {
            std::unique_ptr<rime::Engine> engine(rime::Engine::Create());
            engine->ApplySchema(new rime::Schema("wetype_quanpin"));
            auto config=engine->schema()->config();
            config->SetString("_wetype_context_t2s/opencc_config","s2t.json");
            wetype::ContextSimplifier converter;converter.initialise(engine.get());
            std::string restored;
            require(config->GetString("_wetype_context_t2s/opencc_config",&restored)&&restored=="s2t.json","context converter altered schema configuration");
            wetype::SurroundingText text{true,"我喜歡中國😀 OpenAI","後文與臺灣","選中文字"};
            converter.convert(text);
            require(text.before=="我喜欢中国😀 OpenAI"&&text.after=="后文与台湾"&&text.on=="选中文字","Rime t2s did not convert every context field");
            converter.convert(text);
            require(text.before=="我喜欢中国😀 OpenAI","simplified context changed on repeated conversion");
            wetype::SurroundingText empty;converter.convert(empty);
            require(empty.before.empty()&&empty.after.empty()&&empty.on.empty(),"conversion populated empty context");
            puts("PASS: Rime t2s converts all context fields, preserves emoji/Latin/empty text and restores schema settings");
        }
        auto id=api->create_session();require(id,"cannot create Rime session");
        require(api->select_schema(id,"wetype"),"cannot select wetype schema");
        api->set_option(id,"ascii_mode",false);
        require(api->select_schema(id, "luna_pinyin"), "reference Rime schema unavailable");
        semicolon_punctuation(id, "go");
        require(api->select_schema(id, "wetype_flypy"), "Xiaohe schema unavailable");
        semicolon_punctuation(id, "go");
        {
            std::unique_ptr<rime::Engine> engine(rime::Engine::Create());
            engine->ApplySchema(new rime::Schema("wetype_flypy"));
            auto ctx = engine->context();
            std::string raw(129, 'a');
            ctx->set_input(raw);
            require(!ctx->GetSelectedCandidate(), "worker failure appeared in the candidate menu");
            require(ctx->get_property("_wetype_error") == "invalid input", "worker failure lost its diagnostic");
            std::string committed;
            auto connection = engine->sink().connect([&](const std::string &value) { committed += value; });
            require(engine->ProcessKey(rime::KeyEvent("Return")) && committed == raw,
                    "worker failure prevented committing the raw input");
        }
        require(api->select_schema(id, "wetype"), "cannot restore unified wetype");
        api->set_option(id, "ascii_mode", false);
        for(auto sample:{std::pair<const char *,const char *>{"nihao","ni'hao"},
                         {"nihaozhongguo","ni'hao'zhong'guo"},{"xian","xian"},
                         {"xi'an","xi'an"},{"beijin","bei'jin"},{"codex","codex"},{"nh","n'h"}}){
            type(id,sample.first);expect_preedit(id,sample.second);api->clear_composition(id);
        }
        for(const std::string sample:{"ABC","Codex","OpenAI","iPhone","NiHao","NIHAO","Xi'An"}){
            type(id,sample);expect_spelling(id,sample);
            auto rows=menu(id);require(!rows.empty(),"uppercase input has no engine candidates");
            require(api->select_candidate(id,0),"uppercase candidate selection failed");
            require(commit(id)==rows[0],"uppercase candidate feedback or commit failed");
        }
        // Model Shift+A and Caps Lock input using Rime's real modifier bits.
        api->process_key(id,0xffe1,0);
        require(api->process_key(id,'A',1),"Shift uppercase not handled");
        api->process_key(id,'A',1|(1<<30));api->process_key(id,0xffe1,1<<30);
        require(!api->get_option(id,"ascii_mode"),"Shift letter toggled ASCII mode");
        expect_spelling(id,"A");api->clear_composition(id);
        api->process_key(id,0xffe5,0);api->process_key(id,0xffe5,(1<<30)|2);
        for(char key:std::string("ABC"))require(api->process_key(id,key,2),"Caps uppercase not handled");
        expect_spelling(id,"ABC");api->clear_composition(id);
        type(id,"OpenAI");require(api->process_key(id,0xff08,0),"uppercase delete not handled");
        expect_spelling(id,"OpenA");type(id,"I");expect_spelling(id,"OpenAI");
        require(api->process_key(id,0xff0d,0),"uppercase Return not handled");
        require(commit(id)=="OpenAI","Return did not preserve uppercase source");
        for(const std::string sample:{"abc","beijin","codex","hello","xi'an"}){
            type(id,sample);require(!menu(id).empty(),"prefix test has no candidates");
            require(api->select_candidate(id,0),"prefix candidate selection failed");
            require(!commit(id).empty(),"prefix candidate did not commit");
        }
        type(id,"nihao");auto first=menu(id);require(find(first,"你好")>=0,"native candidate missing");
        int pos=find(first,"拟好");require(pos>=0,"learning candidate missing");
        require(api->select_candidate(id,pos),"API candidate selection failed");
        require(commit(id)=="拟好","selection did not commit expected text");
        type(id,"nihao");auto learned=menu(id);require(find(learned,"拟好")<pos,"learning rank did not improve");
        // Keyboard selection follows the same official notification as the API/mouse path.
        require(api->process_key(id,' ',0),"space not handled");require(!commit(id).empty(),"space did not commit");
        type(id,"nihaozhongguo");auto all=menu(id);pos=find(all,"你好");require(pos>=0,"partial candidate missing");
        require(api->select_candidate(id,pos),"partial selection failed");
        require(commit(id).empty(),"partial selection committed too early");
        auto rest=menu(id);require(!rest.empty()&&rest[0]=="中国","remaining composition incorrect");
        expect_preedit(id,"你好zhong'guo");
        require(api->process_key(id,0xff08,0),"partial backspace not handled");
        // Rime first reopens the selected prefix; it does not remove a letter
        // from the pending suffix until that selection has been undone.
        auto reopened=menu(id);require(find(reopened,"你好中国")>=0,"partial backspace did not reopen the sentence");
        pos=find(reopened,"你好");require(pos>=0,"reopened prefix candidate missing");
        require(api->select_candidate(id,pos),"reselecting reopened prefix failed");
        require(menu(id)[0]=="中国","reselected prefix lost remaining candidates");
        require(api->process_key(id,' ',0),"partial space not handled");require(commit(id)=="你好中国","partial sentence commit incorrect");
        type(id,"nihao");require(api->process_key(id,0xff55,0),"page up not handled");
        require(api->process_key(id,0xff56,0),"page down not handled");auto second=menu(id);require(!second.empty(),"page two empty");
        require(api->process_key(id,0xff1b,0),"escape not handled");require(commit(id).empty(),"escape committed text");
        type(id,"nihao");require(api->process_key(id,0xff08,0),"backspace not handled");
        type(id,"o");require(find(menu(id),"你好")>=0,"backspace/retype broke candidates");
        auto other=api->create_session();require(other,"second Rime session failed");
        require(api->select_schema(other,"wetype"),"second schema failed");api->set_option(other,"ascii_mode",false);
        type(other,"zhongguo");require(menu(other)[0]=="中国","second context candidate wrong");
        auto own=menu(id);require(find(own,"你好")>=0,"contexts contaminated");
        require(api->select_candidate(other,0),"second context selection failed");require(commit(other)=="中国","second context commit wrong");
        api->destroy_session(other);
        require(api->process_key(id,'2',0),"number selection not handled");require(commit(id)==own[1],"number key chose wrong candidate");
        api->set_option(id,"ascii_mode",true);require(!api->process_key(id,'A',0),"ASCII key should pass through");
        api->set_option(id,"ascii_mode",false);
        api->clear_composition(id);
        input_modes(id);
        surrounding_context(id);
        if(argc==6&&std::string(argv[5])=="--timing")timing(id);
        puts("destroying Rime session");api->destroy_session(id);
        int reloads=argc>=5?std::stoi(argv[4]):0;
        for(int cycle=0;cycle<reloads;++cycle){
            printf("reload cycle %d\n",cycle+1);
            api->finalize();api->initialize(&traits);
            api->start_maintenance(false);api->join_maintenance_thread();
            auto session=api->create_session();require(session,"reload session failed");
            for(int change=0;change<3;++change){
                if(argc>=5&&*argv[3]){
                    require(api->select_schema(session,argv[3]),"cannot return to original schema");
                    api->set_option(session,"ascii_mode",false);type(session,"nihao");
                    require(find(menu(session),"你好")>=0,"original schema lost Chinese candidates");
                    api->clear_composition(session);
                }
                require(api->select_schema(session,"wetype"),"wetype reload failed");
                api->set_option(session,"ascii_mode",false);type(session,"nihao");
                require(find(menu(session),"你好")>=0,"wetype reload lost candidates");
                require(api->process_key(session,' ',0),"reload space not handled");
                require(!commit(session).empty(),"reload did not commit");
            }
            api->destroy_session(session);
        }
        puts("finalizing Rime modules");api->finalize();
        puts("PASS: official Rime native module, candidates, API/mouse selection, keyboard commit, learning, partial selection, paging, cancel, backspace, number keys, independent app contexts, ASCII passthrough");
    }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());if(api)api->finalize();return 1;}
}

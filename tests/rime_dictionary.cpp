#include <rime_api.h>
#include <dlfcn.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctime>
static RimeApi *api;
static void modified(const std::string &path){
    // Rime caches source mtimes at one-second resolution. Make rapid edits
    // distinguishable without timing-sensitive sleeps in a redeploy test.
    static int generation=0;timespec times[2]={{std::time(nullptr)+2*(++generation),0},{std::time(nullptr)+2*generation,0}};
    if(utimensat(AT_FDCWD,path.c_str(),times,0))throw std::runtime_error("cannot stamp fixture patch");
}
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static std::string property(RimeSessionId id,const char *name){char value[4096]={};api->get_property(id,name,value,sizeof(value));return value;}
static std::vector<std::string> query(RimeSessionId id,const char *code,bool allow_dictionary_error=false){
    api->clear_composition(id);
    for(const char *p=code;*p;++p)require(api->process_key(id,*p,0),"key not handled");
    RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"missing context");
    std::vector<std::string> rows;
    for(int i=0;i<ctx.menu.num_candidates;++i)rows.emplace_back(ctx.menu.candidates[i].text);
    api->free_context(&ctx);
    auto error=property(id,"_wetype_dictionary_error");if(!allow_dictionary_error&&!error.empty())throw std::runtime_error(error);
    require(property(id,"_wetype_error").empty(),"native input failed");return rows;
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    try{
        require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL),"Lua unavailable");
        api=rime_get_api();const char *modules[]={"default","lua",nullptr};
        RIME_STRUCT(RimeTraits,traits);traits.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";
        traits.user_data_dir=argv[1];traits.log_dir=argv[2];traits.app_name="rime.wetype-dictionary-test";traits.min_log_level=2;traits.modules=modules;
        api->setup(&traits);api->initialize(&traits);
        auto deploy=[&]{api->start_maintenance(true);api->join_maintenance_thread();};
        auto session=[&](const char *schema){auto id=api->create_session();require(api->select_schema(id,schema),"schema unavailable");api->set_option(id,"ascii_mode",false);return id;};
        deploy();auto id=session("wetype");auto rows=query(id,"jihengshangjing");
        require(std::find(rows.begin(),rows.end(),"霁蘅觞璟")==rows.end(),"dictionary enabled by default");
        api->destroy_session(id);api->cleanup_all_sessions();
        std::ofstream settings(std::string(argv[1])+"/wetype.custom.yaml");
        settings<<"patch:\n  translator/dictionary: wetype\n  engine/filters: [simplifier@traditional, uniquifier]\n"
                  "  traditional:\n    option_name: traditionalization\n    opencc_config: s2t.json\n    excluded_types: [user_table]\n";
        settings.close();
        modified(std::string(argv[1])+"/wetype.custom.yaml");
        deploy();id=session("wetype");rows=query(id,"jihengshangjing");
        require(!rows.empty()&&rows[0]=="霁蘅觞璟","imported dictionary missing");
        require(property(id,"_wetype_dictionary_entries")=="5","official columns/import_tables not read");
        api->set_option(id,"traditionalization",true);rows=query(id,"jihengshangjing");
        require(!rows.empty()&&rows[0]=="霽蘅觴璟","ordinary dictionary bypassed OpenCC");
        api->set_option(id,"traditionalization",false);
        // Keep both contexts alive: switching back must reactivate the right
        // dictionary without rebuilding the source table on every key.
        auto enabled=id;
        id=session("wetype_flypy");rows=query(id,"jihguhjk");
        require(std::find(rows.begin(),rows.end(),"霁蘅觞璟")==rows.end(),"dictionary leaked into disabled scheme");
        rows=query(enabled,"jihengshangjing");
        require(!rows.empty()&&rows[0]=="霁蘅觞璟","returning context did not restore its dictionary");
        rows=query(id,"jihguhjk");
        require(std::find(rows.begin(),rows.end(),"霁蘅觞璟")==rows.end(),"disabled context reused another dictionary");
        api->destroy_session(enabled);
        api->destroy_session(id);api->cleanup_all_sessions();
        settings.open(std::string(argv[1])+"/wetype.custom.yaml");settings<<"patch:\n  translator/dictionary: null\n";settings.close();
        modified(std::string(argv[1])+"/wetype.custom.yaml");
        deploy();id=session("wetype");rows=query(id,"jihengshangjing");
        require(std::find(rows.begin(),rows.end(),"霁蘅觞璟")==rows.end(),"disabled dictionary still active");
        api->destroy_session(id);api->cleanup_all_sessions();
        settings.open(std::string(argv[1])+"/wetype.custom.yaml");settings<<"patch:\n  translator/dictionary: missing_dictionary\n";settings.close();
        modified(std::string(argv[1])+"/wetype.custom.yaml");
        deploy();id=session("wetype");rows=query(id,"nihao",true);
        require(!rows.empty()&&rows[0]=="你好","invalid dictionary broke ordinary input");
        require(!property(id,"_wetype_dictionary_error").empty(),"dictionary error not reported");
        api->destroy_session(id);api->finalize();
        puts("PASS: real Rime import_tables, columns, default off, per-scheme configuration and hot redeploy");
    }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

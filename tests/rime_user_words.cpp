#include <rime_api.h>
#include <dlfcn.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
static RimeApi *api;
static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static std::string property(RimeSessionId id,const char *name){char value[4096]={};api->get_property(id,name,value,sizeof(value));return value;}
static std::vector<std::string> rows(RimeSessionId id,const std::string &code){
    api->clear_composition(id);
    for(char ch:code)require(api->process_key(id,ch,0),"key not handled");
    RIME_STRUCT(RimeContext,context);require(api->get_context(id,&context),"missing Rime context");
    std::vector<std::string> result;
    for(int i=0;i<context.menu.num_candidates;++i)result.emplace_back(context.menu.candidates[i].text);
    api->free_context(&context);
    require(property(id,"_wetype_user_words_error").empty(),"Rime stabledb import failed");
    require(property(id,"_wetype_error").empty(),"native query failed");return result;
}
static size_t find(const std::vector<std::string> &values,const std::string &word){
    auto at=std::find(values.begin(),values.end(),word);
    require(at!=values.end(),"configured word missing from native candidates");return at-values.begin();
}
static std::string committed(RimeSessionId id){
    RIME_STRUCT(RimeCommit,value);if(!api->get_commit(id,&value))return {};
    std::string out=value.text;api->free_commit(&value);return out;
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    setvbuf(stdout,nullptr,_IONBF,0);
    try{
        require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL),"Lua plugin unavailable");
        api=rime_get_api();const char *modules[]={"default","lua",nullptr};
        RIME_STRUCT(RimeTraits,traits);traits.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";
        traits.user_data_dir=argv[1];traits.log_dir=argv[2];traits.modules=modules;traits.app_name="rime.wetype-words-test";traits.min_log_level=2;
        api->setup(&traits);api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        auto id=api->create_session();require(id,"cannot create Rime session");
        for(const char *schema:{"wetype","wetype_quanpin","wetype_flypy","wetype_natural","wetype_microsoft","wetype_sogou","wetype_ziguang","wetype_abc","wetype_jiajia","wetype_wubi86","wetype_wubi98","wetype_wubi_new"}){
            require(api->select_schema(id,schema),"scheme unavailable");api->set_option(id,"ascii_mode",false);
            std::string code=std::string(schema).find("wubi")!=std::string::npos?"wu":"wtrime";
            std::string word=code=="wu"?"五笔用户词":"引擎用户词";
            auto menu=rows(id,code);auto index=find(menu,word);
            require(api->select_candidate_on_current_page(id,index),"cannot select configured native word");
            require(committed(id)==word,"configured word commit mismatch");
            printf("PASS native user word and selection in %s\n",schema);
        }
        require(api->select_schema(id,"wetype"),"cannot select wetype");api->set_option(id,"ascii_mode",false);
        auto skipped=rows(id,"negword");
        require(std::find(skipped.begin(),skipped.end(),"禁用用户词")==skipped.end(),"negative table weight imported");
        auto menu=rows(id,"wtrime");size_t index=find(menu,"引擎用户词");
        for(size_t i=0;i<index;++i)require(api->process_key(id,0xff54,0),"cannot highlight user word");
        require(api->process_key(id,0xffff,4),"Control+Delete not handled");
        require(property(id,"_wetype_delete_error").empty(),"native delete notification failed");
        menu=rows(id,"wtrime");
        require(std::find(menu.begin(),menu.end(),"引擎用户词")==menu.end(),"deleted native user word still shown");
        require(committed(id).empty(),"deletion unexpectedly committed text");
        api->clear_composition(id);api->destroy_session(id);api->finalize();
        // Redeployment/reinitialisation reads the edited Rime table, replacing
        // this source's entries without restoring the removed old term.
        std::ofstream table(std::string(argv[1])+"/wetype_user_words.txt");
        table<<"# Edited Rime source\n引擎替换词\twtrime\t20\n";table.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"reload failed");api->set_option(id,"ascii_mode",false);
        menu=rows(id,"wtrime");find(menu,"引擎替换词");
        require(std::find(menu.begin(),menu.end(),"引擎用户词")==menu.end(),"old source term returned after update");
        api->clear_composition(id);api->destroy_session(id);api->finalize();
        std::ofstream settings(std::string(argv[1])+"/wetype.custom.yaml");
        settings<<"patch:\n  wetype_user_words/enabled: false\n";settings.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"disabled scheme failed");api->set_option(id,"ascii_mode",false);
        menu=rows(id,"wtrime");
        require(std::find(menu.begin(),menu.end(),"引擎替换词")==menu.end(),"disabled source still imported");
        find(rows(id,"nihao"),"你好");
        api->clear_composition(id);api->destroy_session(id);api->finalize();
        settings.open(std::string(argv[1])+"/wetype.custom.yaml");
        settings<<"patch:\n  wetype_user_words/db_class: unsupported\n";settings.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"invalid configuration broke schema");api->set_option(id,"ascii_mode",false);
        for(char ch:std::string("nihao"))require(api->process_key(id,ch,0),"ordinary input blocked by configuration error");
        require(!property(id,"_wetype_user_words_error").empty(),"configuration error not reported");
        require(property(id,"_wetype_error").empty(),"configuration error broke native input");
        require(api->process_key(id,' ',0),"ordinary selection failed");require(committed(id)=="你好","ordinary native candidates lost");
        api->destroy_session(id);api->finalize();
        puts("PASS: official Rime stabledb, all 12 schemes, negative weights, Control+Delete, source updates, disabled source and invalid-config fallback");
    }catch(const std::exception &error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}

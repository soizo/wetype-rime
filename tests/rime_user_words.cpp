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
        std::ofstream table(std::string(argv[1])+"/custom_phrase.txt");
        table<<"# Edited Rime source\n引擎替换词\twtrime\t20\n";table.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"reload failed");api->set_option(id,"ascii_mode",false);
        menu=rows(id,"wtrime");find(menu,"引擎替换词");
        require(std::find(menu.begin(),menu.end(),"引擎用户词")==menu.end(),"old source term returned after update");
        api->clear_composition(id);api->destroy_session(id);api->finalize();
        std::ofstream settings(std::string(argv[1])+"/wetype.custom.yaml");
        settings<<"patch:\n  custom_phrase/enable_user_dict: false\n";settings.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"disabled scheme failed");api->set_option(id,"ascii_mode",false);
        menu=rows(id,"wtrime");
        require(std::find(menu.begin(),menu.end(),"引擎替换词")==menu.end(),"disabled source still imported");
        find(rows(id,"nihao"),"你好");
        api->clear_composition(id);api->destroy_session(id);api->finalize();
        std::ofstream personal(std::string(argv[1])+"/用户短语.txt");
        personal<<"自选词表\tunicodeword\t10\n";personal.close();
        settings.open(std::string(argv[1])+"/wetype.custom.yaml");
        settings<<"patch:\n  custom_phrase/user_dict: 用户短语\n";settings.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"personal table patch failed");api->set_option(id,"ascii_mode",false);
        find(rows(id,"unicodeword"),"自选词表");
        api->clear_composition(id);api->destroy_session(id);api->finalize();
        settings.open(std::string(argv[1])+"/wetype.custom.yaml");
        settings<<"patch:\n  custom_phrase/db_class: unsupported\n";settings.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"invalid configuration broke schema");api->set_option(id,"ascii_mode",false);
        for(char ch:std::string("nihao"))require(api->process_key(id,ch,0),"ordinary input blocked by configuration error");
        require(!property(id,"_wetype_user_words_error").empty(),"configuration error not reported");
        require(property(id,"_wetype_error").empty(),"configuration error broke native input");
        require(api->process_key(id,' ',0),"ordinary selection failed");require(committed(id)=="你好","ordinary native candidates lost");
        api->destroy_session(id);api->finalize();
        // A normal Rime component declaration takes ownership of custom_phrase.
        // Non-native codes/weights then belong entirely to table_translator.
        table.open(std::string(argv[1])+"/custom_phrase.txt");
        table<<"原生配置词\twtrime\t10\nRime短语\trr\t999\n";table.close();
        settings.open(std::string(argv[1])+"/wetype.custom.yaml");
        settings<<"patch:\n  \"engine/translators/@before 0\": table_translator@custom_phrase\n  engine/filters: [uniquifier]\n  custom_phrase/initial_quality: 1000\n  custom_phrase/enable_completion: false\n  custom_phrase/enable_sentence: false\n";settings.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"official table translator unavailable");api->set_option(id,"ascii_mode",false);
        find(rows(id,"rr"),"Rime短语");
        RIME_STRUCT(RimeContext,view);require(api->get_context(id,&view),"missing custom phrase context");
        require(view.menu.num_candidates&&std::string(view.menu.candidates[0].text)=="Rime短语","Rime phrase weight ignored");api->free_context(&view);
        require(api->process_key(id,' ',0),"Rime phrase selection failed");require(committed(id)=="Rime短语","Rime phrase commit mismatch");
        rows(id,"nihao");api->set_option(id,"wetype_mode_quanpin",false);api->set_option(id,"wetype_mode_flypy",true);
        require(std::string(api->get_input(id)).empty(),"Rime filter retained composition during a mode change");
        api->destroy_session(id);api->finalize();
        // Removing the optional table component restores native generation;
        // the newly added Rime-only phrase must not have entered its store.
        settings.open(std::string(argv[1])+"/wetype.custom.yaml");settings<<"patch:\n  custom_phrase/user_dict: \"\"\n";settings.close();
        api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        id=api->create_session();require(api->select_schema(id,"wetype"),"native scheme reload failed");api->set_option(id,"ascii_mode",false);
        menu=rows(id,"rr");require(std::find(menu.begin(),menu.end(),"Rime短语")==menu.end(),"official table phrase leaked into native storage");
        api->destroy_session(id);api->finalize();
        puts("PASS: Rime custom_phrase, all 12 schemes, deletion, source edits, disabled/error fallback and official table translator coexistence");
    }catch(const std::exception &error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}

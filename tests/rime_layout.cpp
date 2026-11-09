#include <rime_api.h>
#include <dlfcn.h>
#include <cstdio>
#include <stdexcept>
#include <string>
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    try{
        require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL),"Lua unavailable");
        auto api=rime_get_api();const char *modules[]={"default","lua",nullptr};
        RIME_STRUCT(RimeTraits,t);t.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";
        t.user_data_dir=argv[1];t.log_dir=argv[2];t.modules=modules;t.app_name="rime.wetype-layout";t.min_log_level=2;
        api->setup(&t);api->initialize(&t);api->start_maintenance(true);api->join_maintenance_thread();
        for(const char *schema:{"wetype","wetype_flypy"}){
            auto id=api->create_session();require(id&&api->select_schema(id,schema),"schema unavailable");
            bool pinyin=std::string(schema)=="wetype";
            api->set_option(id,"ascii_mode",false);
            for(char key:std::string(pinyin?"nihao":"nihc"))require(api->process_key(id,key,0),"key rejected");
            RIME_STRUCT(RimeContext,c);require(api->get_context(id,&c),"no context");
            require(c.menu.page_size==(pinyin?5:7),"shared or scheme-specific patch lost");
            require(c.menu.num_candidates>0,"relative runtime paths did not load candidates");
            std::string first=c.menu.candidates[0].text;
            api->free_context(&c);
            require(first==(pinyin?"拟好":"你好"),"layout-scoped YAML pin or native candidate failed");
            char source[64]={};api->get_property(id,"_wetype_context_source",source,sizeof(source));
            require(std::string(source)==(pinyin?"disabled":"unavailable"),"shared or scheme-specific editor-context setting lost");
            require(api->select_candidate(id,0),"native selection rejected");
            RIME_STRUCT(RimeCommit,commit);require(api->get_commit(id,&commit),"missing commit");
            require(first==commit.text,"candidate feedback identity changed");api->free_commit(&commit);
            char error[4096]={};api->get_property(id,"_wetype_error",error,sizeof(error));require(!*error,error);
            api->get_property(id,"_wetype_rules_error",error,sizeof(error));require(!*error,error);
            api->destroy_session(id);
        }
        api->finalize();
        puts("PASS: relative installation paths, shared and scheme patches, layout-scoped YAML rules and native commit");
    }catch(const std::exception &error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}

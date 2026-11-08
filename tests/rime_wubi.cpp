#include <rime_api.h>
#include <dlfcn.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
static RimeApi *api;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
using Rows=std::vector<std::pair<std::string,std::string>>;
static Rows query(RimeSessionId id,const char *code){
    api->clear_composition(id);
    for(auto p=code;*p;++p)require(api->process_key(id,*p,0),"input not handled");
    RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"missing context");Rows rows;
    for(int i=0;i<ctx.menu.num_candidates;++i)rows.emplace_back(ctx.menu.candidates[i].text,ctx.menu.candidates[i].comment?ctx.menu.candidates[i].comment:"");
    api->free_context(&ctx);char error[4096]={};api->get_property(id,"_wetype_error",error,sizeof(error));require(!*error,error);
    return rows;
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    try{
        require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL),"Lua unavailable");
        api=rime_get_api();const char *modules[]={"default","lua",nullptr};RIME_STRUCT(RimeTraits,traits);
        traits.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";traits.user_data_dir=argv[1];traits.log_dir=argv[2];traits.modules=modules;traits.app_name="rime.wetype-wubi-test";traits.min_log_level=2;
        api->setup(&traits);api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
        for(const char *mode:{"wubi86","wubi98","wubi_new"})for(bool unified:{false,true}){
            auto id=api->create_session();auto schema=unified?std::string("wetype"):std::string("wetype_")+mode;
            require(api->select_schema(id,schema.c_str()),"scheme unavailable");api->set_option(id,"ascii_mode",false);
            if(unified)api->set_option(id,(std::string("wetype_mode_")+mode).c_str(),true);
            require(api->get_option(id,"wetype_wubi_pinyin")&&api->get_option(id,"wetype_wubi_comment"),"Wubi assistance not enabled by default");
            auto rows=query(id,"nihao");require(!rows.empty()&&rows[0]==std::make_pair(std::string("你好"),std::string("wqvb")),"pinyin/code comment mismatch");
            require(api->process_key(id,' ',0),"selection failed");RIME_STRUCT(RimeCommit,commit);require(api->get_commit(id,&commit),"missing commit");require(std::string(commit.text)=="你好","pinyin selection feedback mismatch");api->free_commit(&commit);
            rows=query(id,"wq");bool suffix=false;for(auto &row:rows)if(!row.second.empty())suffix=true;require(suffix,"missing remaining-code hints");
            rows=query(id,"zz");require(!rows.empty()&&!rows[0].second.empty(),"missing wildcard hints");
            api->set_option(id,"wetype_wubi_comment",false);require(std::string(api->get_input(id)).empty(),"toggle retained stale composition");
            rows=query(id,"nihao");require(rows[0].first=="你好"&&rows[0].second.empty(),"comment toggle affected pinyin");
            api->set_option(id,"wetype_wubi_pinyin",false);rows=query(id,"nihao");for(auto &row:rows)require(row.first!="你好","pinyin disabled incorrectly");
            api->set_option(id,"wetype_wubi_comment",true);rows=query(id,"wq");suffix=false;for(auto &row:rows)if(!row.second.empty())suffix=true;require(suffix,"comment toggle did not restore hints");
            rows=query(id,"nihao");for(auto &row:rows)require(row.first!="你好","comment toggle enabled pinyin");
            api->set_option(id,"wetype_wubi_pinyin",true);rows=query(id,"nihao");require(rows[0].second=="wqvb","pinyin toggle did not restore codes");
            api->destroy_session(id);
        }
        api->finalize();puts("PASS: all three Wubi variants, standalone/unified defaults, native pinyin selection, code/wildcard hints and independent live toggles");
    }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

#include <rime_api.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
static RimeApi *api;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static std::string property(RimeSessionId id,const char *key){char value[4096]={};api->get_property(id,key,value,sizeof(value));return value;}
static void patch(const std::string &root,const std::string &selection){
    auto file=root+"/wetype.custom.yaml";std::ofstream out(file);
    out<<"patch:\n  wetype/emoji_option: emoji\n  wetype/cell_dicts: "<<selection<<'\n';out.close();
    static int generation=0;auto stamp=std::time(nullptr)+2*(++generation);timespec times[2]={{stamp,0},{stamp,0}};
    require(!utimensat(AT_FDCWD,file.c_str(),times,0),"cannot stamp fixture patch");
}
static std::vector<std::string> query(RimeSessionId id,const char *code,const char *count,bool allow_error=false){
    api->clear_composition(id);for(const char *p=code;*p;++p)require(api->process_key(id,*p,0),"key not handled");
    RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"missing context");std::vector<std::string> rows;
    for(int i=0;i<ctx.menu.num_candidates;++i)rows.emplace_back(ctx.menu.candidates[i].text);api->free_context(&ctx);
    auto error=property(id,"_wetype_cell_dicts_error");if(!allow_error&&!error.empty())throw std::runtime_error(error);
    if(!allow_error)require(property(id,"_wetype_cell_dicts_count")==count,"wrong active native cell count");
    require(property(id,"_wetype_error").empty(),"native input unavailable");return rows;
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    try{
        require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL),"Lua unavailable");
        api=rime_get_api();const char *modules[]={"default","lua",nullptr};RIME_STRUCT(RimeTraits,traits);
        traits.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";traits.user_data_dir=argv[1];traits.log_dir=argv[2];
        traits.app_name="rime.wetype-cells-test";traits.min_log_level=2;traits.modules=modules;api->setup(&traits);api->initialize(&traits);
        auto deploy=[&]{api->start_maintenance(true);api->join_maintenance_thread();};
        auto session=[&](const char *schema){auto id=api->create_session();require(api->select_schema(id,schema),"scheme unavailable");api->set_option(id,"ascii_mode",false);api->set_option(id,"emoji",true);return id;};
        patch(argv[1],"[]");deploy();auto id=session("wetype");auto baseline=query(id,"wuqiong","0");require(baseline.size()>=3,"missing baseline");
        api->destroy_session(id);api->cleanup_all_sessions();
        patch(argv[1],"[cell_dict.sc]");deploy();id=session("wetype");auto rows=query(id,"wuqiong","1");require(rows[0]=="无穷","native cell ranking not applied");
        auto other=session("wetype_flypy");query(other,"wuqs","0");rows=query(id,"wuqiong","1");require(rows[0]=="无穷","another context disabled this scheme's cells");
        query(other,"wuqs","0");api->destroy_session(other);api->destroy_session(id);api->cleanup_all_sessions();
        patch(argv[1],"[cell_dict.sc, cell_dict.ssrc, cell_dict.hf]");deploy();id=session("wetype");query(id,"wuqiong","3");
        api->destroy_session(id);api->cleanup_all_sessions();
        patch(argv[1],"[]");deploy();id=session("wetype");rows=query(id,"wuqiong","0");
        require(rows.size()>=3&&std::vector<std::string>(rows.begin(),rows.begin()+3)==std::vector<std::string>(baseline.begin(),baseline.begin()+3),"disabled cell ranking remained");
        api->destroy_session(id);api->cleanup_all_sessions();
        for(const auto &invalid:{std::string("[missing_cell]"),std::string("not_a_list")}){
            patch(argv[1],invalid);deploy();id=session("wetype");rows=query(id,"nihao","0",true);
            require(!property(id,"_wetype_cell_dicts_error").empty(),"native cell error missing");require(!rows.empty()&&rows[0]=="你好","bad cell configuration blocked input");
            api->destroy_session(id);api->cleanup_all_sessions();
        }
        api->finalize();puts("PASS: real Rime default off, individual/all cells, independent contexts, hot redeploy, disable and invalid configuration fallback");
    }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

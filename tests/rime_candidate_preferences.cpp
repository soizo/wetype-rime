#include <rime_api.h>
#include <dlfcn.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
static RimeApi *api;
static void require(bool ok,const char *msg){if(!ok)throw std::runtime_error(msg);}
static std::vector<std::string> rows(RimeSessionId id){
 RIME_STRUCT(RimeContext,c);require(api->get_context(id,&c),"no context");std::vector<std::string> out;
 for(int i=0;i<c.menu.num_candidates;++i)out.emplace_back(c.menu.candidates[i].text);api->free_context(&c);return out;
}
static auto query(RimeSessionId id,const std::string &code){api->clear_composition(id);for(auto c:code)api->process_key(id,c,0);return rows(id);}
static void action(RimeSessionId id,char key){require(api->process_key(id,key,5),"action not consumed");char error[4096]={};api->get_property(id,"_wetype_candidate_error",error,sizeof(error));require(!*error,error);}
int main(int argc,char **argv){
 if(argc!=3)return 2;
 try{
 require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL),"Lua unavailable");
 api=rime_get_api();const char *modules[]={"default","lua",nullptr};RIME_STRUCT(RimeTraits,t);
 t.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";t.user_data_dir=argv[1];t.log_dir=argv[2];t.modules=modules;t.app_name="rime.wetype-preferences";t.min_log_level=2;
 api->setup(&t);api->initialize(&t);api->start_maintenance(true);api->join_maintenance_thread();
 for(const char *schema:{"wetype","wetype_wubi86"}){
  auto session=[&]{auto id=api->create_session();require(api->select_schema(id,schema),"schema unavailable");api->set_option(id,"ascii_mode",false);return id;};
  std::string code=std::string(schema)=="wetype"?"nihao":"wq";
  auto id=session();auto baseline=query(id,code);require(baseline.size()>=3,"too few candidates");auto target=baseline[1];
  require(api->highlight_candidate(id,1),"highlight failed");action(id,'p');require(rows(id)[0]==target,"pin did not promote selected candidate");
  api->destroy_session(id);id=session();require(query(id,code)[0]==target,"pin lost across sessions");
  action(id,'b');auto blocked=rows(id);require(std::find(blocked.begin(),blocked.end(),target)==blocked.end(),"blocked word remained");
  api->destroy_session(id);id=session();blocked=query(id,code);require(std::find(blocked.begin(),blocked.end(),target)==blocked.end(),"block lost across sessions");
  action(id,'u');require(rows(id)==baseline,"restore changed unrelated ranking");
  require(api->highlight_candidate(id,1),"highlight failed");action(id,'p');action(id,'r');require(rows(id)==baseline,"unpin did not restore ordering");
  require(api->highlight_candidate(id,1),"highlight failed");action(id,'p');require(api->process_key(id,' ',0),"commit failed");
  RIME_STRUCT(RimeCommit,c);require(api->get_commit(id,&c),"no commit");require(target==c.text,"pin changed selection identity");api->free_commit(&c);
  api->destroy_session(id);
 }
 api->finalize();puts("PASS: pin/block persistence, independent restoration, unchanged remainder and original candidate commit in pinyin and Wubi");
 }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

#include <rime_api.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctime>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
static RimeApi *api;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static std::string property(RimeSessionId id,const char *key){char value[4096]={};api->get_property(id,key,value,sizeof(value));return value;}
static std::vector<std::string> rows(RimeSessionId id,const char *code){
 api->clear_composition(id);for(const char *p=code;*p;++p)require(api->process_key(id,*p,0),"key not handled");
 RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"missing Rime context");std::vector<std::string> result;
 for(int i=0;i<ctx.menu.num_candidates;++i)result.emplace_back(ctx.menu.candidates[i].text);api->free_context(&ctx);
 auto error=property(id,"_wetype_error");if(!error.empty())throw std::runtime_error(error);return result;
}
static bool contains(const std::vector<std::string> &rows,const std::string &word){return std::find(rows.begin(),rows.end(),word)!=rows.end();}
static std::string committed(RimeSessionId id){RIME_STRUCT(RimeCommit,commit);if(!api->get_commit(id,&commit))return {};std::string text=commit.text;api->free_commit(&commit);return text;}
int main(int argc,char **argv){
 if(argc!=3&&argc!=4)return 2;
 try{
  require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL),"Lua unavailable");
  api=rime_get_api();const char *modules[]={"default","lua",nullptr};RIME_STRUCT(RimeTraits,traits);
  traits.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";traits.user_data_dir=argv[1];traits.log_dir=argv[2];traits.modules=modules;traits.app_name="rime.wetype-expressions-test";traits.min_log_level=2;
  api->setup(&traits);api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
  const char *schema=argc==4?argv[3]:"wetype";bool flypy=std::string(schema).find("flypy")!=std::string::npos;
  const char *happy=flypy?"kdxb":"kaixin",*stars=flypy?"xkxk":"xingxing",*hello=flypy?"nihc":"nihao";
  auto session=[&]{auto id=api->create_session();require(api->select_schema(id,schema),"scheme unavailable");api->set_option(id,"ascii_mode",false);api->set_option(id,"traditionalization",false);api->set_option(id,"emoji",false);return id;};
  auto id=session();require(contains(rows(id,happy),"开心(*^▽^*)"),"kaomoji not enabled by default");
  require(contains(rows(id,stars),"☆"),"symbols not enabled by default");
  require(property(id,"_wetype_expression_flags")=="6","wrong default expression flags");
  api->set_option(id,"kaomoji",false);api->set_option(id,"symbol_emoji",false);
  require(!contains(rows(id,happy),"开心(*^▽^*)"),"disabled native kaomoji remained");
  require(!contains(rows(id,"uukao"),"后(๑•̀ㅂ•́)و✧"),"disabled custom kaomoji remained");
  api->set_option(id,"kaomoji",true);require(std::string(api->get_input(id)).empty(),"expression toggle retained stale composition");
  require(contains(rows(id,happy),"开心(*^▽^*)"),"native kaomoji missing");require(property(id,"_wetype_expression_flags")=="2","wrong native kaomoji flags");
  api->set_option(id,"traditionalization",true);auto menu=rows(id,"uukao");require(menu[0]=="后(๑•̀ㅂ•́)و✧","custom kaomoji priority/literal text lost");
  require(api->process_key(id,' ',0),"custom kaomoji selection failed");require(committed(id)=="后(๑•̀ㅂ•́)و✧","custom kaomoji commit changed text");
  api->set_option(id,"kaomoji",false);require(!contains(rows(id,"uukao"),"后(๑•̀ㅂ•́)و✧"),"disabled custom kaomoji remained");
  api->set_option(id,"traditionalization",false);api->set_option(id,"symbol_emoji",true);require(contains(rows(id,stars),"☆"),"native symbol missing");
  menu=rows(id,"uusym");require(menu[0]=="☆彡","custom symbol priority missing");require(api->process_key(id,' ',0),"custom symbol selection failed");require(committed(id)=="☆彡","custom symbol commit mismatch");
  api->set_option(id,"emoji_plus",true);rows(id,hello);require(property(id,"_wetype_expression_flags")=="4","obsolete binding reactivated Emoji Plus");
  api->set_option(id,"symbol_emoji",false);require(!contains(rows(id,"uusym"),"☆彡"),"disabled custom symbol remained");
  api->set_option(id,"emoji_plus",false);rows(id,hello);require(property(id,"_wetype_expression_flags")=="0","expression flags did not reset");
  api->destroy_session(id);api->cleanup_all_sessions();
  auto file=std::string(argv[1])+"/kaomoji.txt";std::ofstream updated(file);updated<<"新增颜文字(≧▽≦)\tuunew\t100\n";updated.close();
  auto stamp=std::time(nullptr)+2;timespec times[2]={{stamp,0},{stamp,0}};require(!utimensat(AT_FDCWD,file.c_str(),times,0),"cannot stamp edited expression table");
  api->start_maintenance(true);api->join_maintenance_thread();id=session();api->set_option(id,"kaomoji",true);
  menu=rows(id,"uunew");require(menu[0]=="新增颜文字(≧▽≦)","edited personal expression table not loaded after deploy");
  require(!contains(rows(id,"uukao"),"后(๑•̀ㅂ•́)و✧"),"removed expression survived redeploy");
  api->destroy_session(id);api->finalize();puts("PASS: native expression switches, official Rime personal tables, literal OpenCC output, commit, edits, removal and redeploy");
 }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}

// Acceptance for the external Lua expression tables: candidate matching on
// simplified text, filter order, comments, option gating, hot reload, commit,
// the native-engine step-aside guard and literal OpenCC output.
#include <rime_api.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
static RimeApi *api;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static std::string property(RimeSessionId id,const char *key){char value[4096]={};api->get_property(id,key,value,sizeof(value));return value;}
using Row=std::pair<std::string,std::string>;
static std::vector<Row> rows(RimeSessionId id,const char *code){
 api->clear_composition(id);for(const char *p=code;*p;++p)require(api->process_key(id,*p,0),"key not handled");
 RIME_STRUCT(RimeContext,ctx);require(api->get_context(id,&ctx),"missing Rime context");std::vector<Row> result;
 for(int i=0;i<ctx.menu.num_candidates;++i)
  result.emplace_back(ctx.menu.candidates[i].text,ctx.menu.candidates[i].comment?ctx.menu.candidates[i].comment:"");
 api->free_context(&ctx);
 auto error=property(id,"_wetype_error");if(!error.empty())throw std::runtime_error(error);
 auto expressions=property(id,"_wetype_expressions_error");if(!expressions.empty())throw std::runtime_error(expressions);
 return result;}
static bool contains(const std::vector<Row> &rows,const std::string &text){
 return std::any_of(rows.begin(),rows.end(),[&](const Row &row){return row.first==text;});}
static std::string committed(RimeSessionId id){RIME_STRUCT(RimeCommit,commit);if(!api->get_commit(id,&commit))return {};std::string text=commit.text;api->free_commit(&commit);return text;}
int main(int argc,char **argv){
 if(argc!=3)return 2;
 const std::string user=argv[1];
 try{
  require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",RTLD_NOW|RTLD_GLOBAL),"Lua unavailable");
  api=rime_get_api();const char *modules[]={"default","lua",nullptr};RIME_STRUCT(RimeTraits,traits);
  traits.shared_data_dir="/Library/Input Methods/Squirrel.app/Contents/SharedSupport";traits.user_data_dir=argv[1];traits.log_dir=argv[2];traits.modules=modules;traits.app_name="rime.wetype-lua-expressions-test";traits.min_log_level=2;
  api->setup(&traits);api->initialize(&traits);api->start_maintenance(true);api->join_maintenance_thread();
  auto session=[&]{auto id=api->create_session();require(api->select_schema(id,"wetype"),"scheme unavailable");api->set_option(id,"ascii_mode",false);return id;};

  // Phase 1: the external tables alone. Fixture rows live in the personal
  // overlays; the supplied shipped tables are installed but not asserted on.
  auto id=session();
  auto menu=rows(id,"ceshi");
  require(menu.size()>=4,"expected the fixture expressions beside the word");
  require(menu[0].first=="测试","word candidate lost the first position");
  require(menu[1].first=="※"&&menu[2].first=="🍄"&&menu[3].first=="测试(╯°□°)╯","expression filter order incorrect");
  require(menu[3].second=="测试","kaomoji comment must name the source word");
  require(menu[1].second.empty()&&menu[2].second.empty(),"symbol/emoji candidates must stay uncommented");
  // Selecting an expression commits its literal text.
  require(api->select_candidate(id,2),"expression selection failed");
  require(committed(id)=="🍄","expression commit changed text");
  // Options gate each category independently.
  api->set_option(id,"emoji",false);require(!contains(rows(id,"ceshi"),"🍄"),"disabled emoji remained");
  require(contains(rows(id,"ceshi"),"※"),"disabling emoji hid another category");
  api->set_option(id,"symbol_emoji",false);require(!contains(rows(id,"ceshi"),"※"),"disabled symbols remained");
  api->set_option(id,"kaomoji",false);require(!contains(rows(id,"ceshi"),"测试(╯°□°)╯"),"disabled kaomoji remained");
  api->set_option(id,"emoji",true);api->set_option(id,"symbol_emoji",true);api->set_option(id,"kaomoji",true);
  // Personal overlays hot-reload when their bytes change; no redeploy needed.
  {std::ofstream out(user+"/wetype_emoji.custom.tsv",std::ios::app);out<<"测试\t🎃\n";}
  menu=rows(id,"ceshi");require(contains(menu,"🎃"),"edited personal table not hot-reloaded");
  // Hybrid matching: a /pattern/ key is a Lua pattern, not an exact literal.
  {std::ofstream out(user+"/wetype_emoji.custom.tsv",std::ios::app);out<<"/^测/\t🧪\n";}
  require(contains(rows(id,"ceshi"),"🧪"),"Lua pattern key did not match 测试");
  require(!contains(rows(id,"nihao"),"🧪"),"pattern leaked to a non-matching input");
  api->destroy_session(id);

  // Phase 2: binding the native engine must switch the Lua tables aside, and
  // a later OpenCC conversion must keep expression text literal.
  {std::ofstream out(user+"/wetype.custom.yaml");
   out<<"patch:\n"
      <<"  wetype/emoji_option: emoji\n"
      <<"  \"engine/filters/+\":\n"
      <<"    - simplifier@traditional\n"
      <<"    - uniquifier\n"
      <<"  traditional:\n"
      <<"    option_name: traditionalization\n"
      <<"    opencc_config: s2t.json\n"
      <<"    tips: none\n"
      <<"    excluded_types: [user_table, lua_emoji, lua_kaomoji, lua_symbols]\n";}
  auto stamp=std::time(nullptr)+2;timespec times[2]={{stamp,0},{stamp,0}};
  require(!utimensat(AT_FDCWD,(user+"/wetype.custom.yaml").c_str(),times,0),"cannot stamp the patch");
  api->start_maintenance(true);api->join_maintenance_thread();
  id=session();api->set_option(id,"traditionalization",true);
  menu=rows(id,"ceshi");
  require(!contains(menu,"🍄")&&!contains(menu,"🎃"),"native binding did not switch the Lua emoji table aside");
  require(property(id,"_wetype_expression_flags")=="1","native emoji flag missing");
  require(contains(menu,"测试(╯°□°)╯"),"OpenCC conversion altered literal kaomoji text");
  api->set_option(id,"emoji",false);
  menu=rows(id,"ceshi");
  require(property(id,"_wetype_expression_flags")=="0","expression flags did not reset");
  require(!contains(menu,"🍄")&&!contains(menu,"🎃"),"the shared option must gate native and external emoji together");
  api->destroy_session(id);api->cleanup_all_sessions();api->finalize();
  puts("PASS: external Lua expression tables, order, comments, commit, options, hot reload, native step-aside and literal OpenCC output");
 }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
 return 0;}

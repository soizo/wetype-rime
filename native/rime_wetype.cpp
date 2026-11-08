// Rime's official Translator, Candidate, Context notifications and module API.
#include <rime_api.h>
#include <rime/translator.h>
#include <rime/translation.h>
#include <rime/candidate.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/segmentation.h>
#include <rime/module.h>
#include <rime/registry.h>
#include <rime/processor.h>
#include <rime/key_event.h>
#include <wetype_modes.h>
#include <dlfcn.h>
#include <map>
#include <memory>
#include <future>
#include <functional>
#include "engine_client.h"
#include "surrounding_text.h"
#include "rime_fuzzy.h"
#include "rime_user_words.h"
namespace wetype {
static const InputMode &selected_mode(rime::Engine *engine){
    string fixed;engine->schema()->config()->GetString("wetype/mode",&fixed);
    if(!fixed.empty()&&fixed!="selectable")return input_mode(fixed);
    for(const auto &mode:input_modes)
        if(engine->context()->get_option(string("wetype_mode_")+mode.id))return mode;
    return input_mode("quanpin");
}
// Keep the official speller's editing/selection behavior. Only reserve the
// semicolon for layouts that actually use it; otherwise the punctuator handles it.
class ModeSpeller:public rime::Processor {
    std::unique_ptr<rime::Processor> speller;
public:
    explicit ModeSpeller(const rime::Ticket &ticket):rime::Processor(ticket),
        speller(rime::Processor::Require("speller")->Create(ticket)){}
    rime::ProcessResult ProcessKeyEvent(const rime::KeyEvent &key) override{
        const auto &mode=selected_mode(engine_);
        if(key.keycode()==';'&&!mode.semicolon)return rime::kNoop;
        auto ctx=engine_->context();
        // Match the official speller's max-code-length selection behavior,
        // but only for Wubi; pinyin remains an unrestricted composition.
        if(mode.keyboard==5&&!key.release()&&!key.ctrl()&&!key.alt()&&!key.super()&&
           key.keycode()>='a'&&key.keycode()<='z'&&ctx->caret_pos()==ctx->input().size()){
            auto candidate=ctx->GetSelectedCandidate();
            if(candidate&&candidate->end()==ctx->input().size()&&candidate->end()-candidate->start()==4)
                ctx->ConfirmCurrentSelection();
        }
        return speller->ProcessKeyEvent(key);
    }
};
using Preparing=std::shared_future<std::shared_ptr<Client>>;
static std::map<string,Preparing> clients;
static std::mutex clients_mu;
static Preparing prepare(const string &launcher){
    std::lock_guard<std::mutex> lock(clients_mu);
    auto &pending=clients[launcher];
    if(!pending.valid())pending=std::async(std::launch::async,[launcher]{return std::make_shared<Client>(launcher);}).share();
    return pending;
}
class WetypeCandidate:public rime::SimpleCandidate {
public:
    uint64_t ticket,session;size_t index;
    string mode;
    WetypeCandidate(size_t start,const Row &row,uint64_t revision,uint64_t id,const string &mode_id)
        :rime::SimpleCandidate("wetype",start,start+row.cover,row.text,"",row.preedit),ticket(revision),session(id),index(row.index),mode(mode_id){}
};
class DeferredTranslation:public rime::Translation {
    std::function<rime::an<rime::Translation>()> load;
    rime::an<rime::Translation> result;
    void prepare(){
        if(!load)return;
        result=load();load={};set_exhausted(!result||result->exhausted());
    }
public:
    explicit DeferredTranslation(std::function<rime::an<rime::Translation>()> loader):load(std::move(loader)){}
    rime::an<rime::Candidate> Peek() override{prepare();return exhausted()?nullptr:result->Peek();}
    bool Next() override{
        prepare();if(exhausted())return false;
        bool next=result->Next();set_exhausted(result->exhausted());return next;
    }
};
class Translator:public rime::Translator {
    std::shared_ptr<Client> host;
    Preparing preparing;
    uint64_t session=0;
    rime::connection selected,updated,option_changed,deleted;
    bool active=false;
    bool emoji_enabled=false;
    bool fuzzy_enabled=false;
    RimeFuzzy fuzzy;
    std::vector<uint64_t> fuzzy_sessions;
    RimeUserWords user_words;
    std::shared_future<void> words_preparing;
    bool words_valid=false;
    string launcher,mode_id;
    string before_text;
    string logged_context_source;
    bool context_sampled=false;
    std::shared_ptr<int> lifetime=std::make_shared<int>(0);
    bool requested_emoji() const {
        string option;
        return engine_->schema()->config()->GetString("wetype/emoji_option",&option)
            &&!option.empty()&&engine_->context()->get_option(option);
    }
    bool requested_fuzzy() const {
        string option;
        return engine_->schema()->config()->GetString("wetype/fuzzy_option",&option)
            &&!option.empty()&&engine_->context()->get_option(option);
    }
    void choose_mode(){
        const auto &mode=selected_mode(engine_);
        bool emoji=requested_emoji();
        bool use_fuzzy=requested_fuzzy();
        if(mode_id==mode.id&&emoji_enabled==emoji&&fuzzy_enabled==use_fuzzy)return;
        string path;
        auto config=engine_->schema()->config();
        if(!config->GetString(string("wetype/launchers/")+mode.group,&path))
            config->GetString("wetype/launcher",&path);
        if(path.empty())throw std::runtime_error("wetype launcher not configured");
        if(host&&session)host->command("CLOSE "+std::to_string(session));
        if(host)for(auto id:fuzzy_sessions)host->command("CLOSE "+std::to_string(id));
        fuzzy_sessions.clear();
        session=0;active=false;before_text.clear();context_sampled=false;
        bool changed_host=path!=launcher;
        if(changed_host){host.reset();preparing={};words_preparing={};launcher=path;}
        mode_id=mode.id;
        emoji_enabled=emoji;
        fuzzy_enabled=use_fuzzy;
        try{
            if(fuzzy_enabled)fuzzy.load(config,mode_id);
            engine_->context()->set_property("_wetype_fuzzy_error","");
        }catch(const std::exception &e){
            engine_->context()->set_property("_wetype_fuzzy_error",e.what());
        }
        if(!preparing.valid())preparing=prepare(launcher);
        if(words_valid&&!words_preparing.valid()&&!user_words.source.empty()){
            auto pending=preparing;auto words=user_words;
            words_preparing=std::async(std::launch::async,[pending,words]{pending.get()->sync_words(words.source,words.entries);}).share();
        }
        engine_->context()->set_property("_wetype_mode",mode_id);
    }
    void ensure(){
        choose_mode();if(!host)host=preparing.get();
        if(words_preparing.valid()){
            try{words_preparing.get();engine_->context()->set_property("_wetype_user_words_error","");}
            catch(const std::exception &e){engine_->context()->set_property("_wetype_user_words_error",e.what());}
        }
        if(!session)session=host->open(mode_id,emoji_enabled);
    }
    void error(const std::exception &e){
        string path;if(engine_->schema()->config()->GetString("wetype/log_file",&path))
            if(FILE *f=fopen(path.c_str(),"a")){fprintf(f,"[wetype] %s\n",e.what());fclose(f);}
        engine_->context()->set_property("_wetype_error",e.what());
    }
    rime::an<rime::Translation> snapshot(const string &input,size_t start,size_t end){
        auto out=std::make_shared<rime::FifoTranslation>();
        try{
            ensure();
            auto ctx=engine_->context();
            if(!context_sampled){
                if(ctx->get_option("wetype_surrounding")){
                    auto context=surrounding_text();before_text=context.before;
                    ctx->set_property("_wetype_context_source",context.available?"imk":"unavailable");
                }else{before_text.clear();ctx->set_property("_wetype_context_source","disabled");}
                ctx->set_property("_wetype_context_bytes",std::to_string(before_text.size()));
                string source=ctx->get_property("_wetype_context_source");
                if(source!=logged_context_source){
                    string path;
                    if(engine_->schema()->config()->GetString("wetype/log_file",&path))
                        if(FILE *f=fopen(path.c_str(),"a")){fprintf(f,"[wetype] surrounding source=%s bytes=%zu\n",source.c_str(),before_text.size());fclose(f);}
                    logged_context_source=source;
                }
                context_sampled=true;
            }
            auto result=host->query(session,input,before_text);active=true;
            auto variants=fuzzy_enabled&&ctx->get_property("_wetype_fuzzy_error").empty()?fuzzy.variants(input):std::vector<string>{};
            std::set<std::pair<string,size_t>> seen;
            auto append=[&](const Row &row,uint64_t ticket,uint64_t id,bool is_fuzzy){
                if(!variants.empty()&&!seen.insert({row.text,row.cover}).second)return;
                auto candidate=std::make_shared<WetypeCandidate>(start,row,ticket,id,mode_id);
                if(is_fuzzy){candidate->set_type("wetype_fuzzy");candidate->set_comment("模糊");}
                out->Append(candidate);
            };
            // Keep the first three exact candidates, then bounded fuzzy choices.
            // Each branch owns a separate native session and selection ticket.
            size_t head=variants.empty()?result.rows.size():std::min<size_t>(3,result.rows.size());
            for(size_t i=0;i<head;++i)append(result.rows[i],result.ticket,session,false);
            ctx->set_property("_wetype_fuzzy_variants",std::to_string(variants.size()));
            for(size_t i=0;i<variants.size();++i){
                try{
                    if(i>=fuzzy_sessions.size())fuzzy_sessions.push_back(host->open("quanpin",emoji_enabled));
                    auto alternative=host->query(fuzzy_sessions[i],variants[i],before_text);
                    size_t count=0;
                    for(auto row:alternative.rows){
                        // Only complete branch matches are exposed. Partial
                        // selection continues through the untouched native path.
                        if(row.cover!=variants[i].size())continue;
                        row.cover=input.size();row.preedit=input;
                        append(row,alternative.ticket,fuzzy_sessions[i],true);
                        if(++count==3)break;
                    }
                }catch(const std::exception &e){ctx->set_property("_wetype_fuzzy_error",e.what());}
            }
            for(size_t i=head;i<result.rows.size();++i)append(result.rows[i],result.ticket,session,false);
            engine_->context()->set_property("_wetype_error","");
        }catch(const std::exception &e){
            error(e);out->Append(std::make_shared<rime::SimpleCandidate>("wetype_error",start,end,input,"引擎暂不可用，回车保留原文"));
        }
        return out;
    }
public:
    explicit Translator(const rime::Ticket &ticket):rime::Translator(ticket){
        // Only pipe/worker startup runs in the background; all Rime objects and
        // candidate/context operations remain on the frontend's own thread.
        auto ctx=engine_->context();
        try{user_words.load(engine_->schema()->config());words_valid=true;ctx->set_property("_wetype_user_words_error","");}
        catch(const std::exception &e){ctx->set_property("_wetype_user_words_error",e.what());}
        choose_mode();
        Dl_info image{};
        if(dladdr(reinterpret_cast<void *>(&prepare),&image)&&image.dli_fname){
            ctx->set_property("_wetype_adapter",image.dli_fname);
            string log;
            if(engine_->schema()->config()->GetString("wetype/log_file",&log))
                if(FILE *f=fopen(log.c_str(),"a")){fprintf(f,"[wetype] adapter ready: %s\n",image.dli_fname);fclose(f);}
        }
        // Grouped official notifications precede Rime's ungrouped OnSelect;
        // selection is delivered before it advances the composition segment.
        selected=ctx->select_notifier().connect(-100,[this](rime::Context *c){
            auto candidate=rime::Candidate::GetGenuineCandidate(c->GetSelectedCandidate());
            auto own=std::dynamic_pointer_cast<WetypeCandidate>(candidate);
            if(!own||!host||own->mode!=mode_id)return;
            if(own->session!=session&&std::find(fuzzy_sessions.begin(),fuzzy_sessions.end(),own->session)==fuzzy_sessions.end())return;
            try{host->command("SELECT "+std::to_string(own->session)+" "+std::to_string(own->ticket)+" "+std::to_string(own->index));}
            catch(const std::exception &e){error(e);}
        });
        deleted=ctx->delete_notifier().connect(-100,[this](rime::Context *c){
            auto own=std::dynamic_pointer_cast<WetypeCandidate>(rime::Candidate::GetGenuineCandidate(c->GetSelectedCandidate()));
            if(!own||!host||own->mode!=mode_id)return;
            if(own->session!=session&&std::find(fuzzy_sessions.begin(),fuzzy_sessions.end(),own->session)==fuzzy_sessions.end())return;
            try{
                host->command("DELETE "+std::to_string(own->session)+" "+std::to_string(own->ticket)+" "+std::to_string(own->index));
                c->set_property("_wetype_delete_error","");
                c->RefreshNonConfirmedComposition();
            }catch(const std::exception &e){c->set_property("_wetype_delete_error",e.what());}
        });
        updated=ctx->update_notifier().connect(-100,[this](rime::Context *c){
            if(c->input().empty()){
                before_text.clear();context_sampled=false;c->set_property("_wetype_context_bytes","0");
                if(active&&host){active=false;try{
                    host->command("RESET "+std::to_string(session));
                    for(auto id:fuzzy_sessions)host->command("RESET "+std::to_string(id));
                }catch(const std::exception &e){error(e);}}
            }
        });
        option_changed=ctx->option_update_notifier().connect([this](rime::Context *c,const string &name){
            string fuzzy_option;
            if(engine_->schema()->config()->GetString("wetype/fuzzy_option",&fuzzy_option)&&name==fuzzy_option){
                try{choose_mode();c->Clear();}catch(const std::exception &e){error(e);}
                return;
            }
            string emoji_option;
            if(engine_->schema()->config()->GetString("wetype/emoji_option",&emoji_option)&&name==emoji_option){
                try{choose_mode();c->Clear();}catch(const std::exception &e){error(e);}
                return;
            }
            if(name=="wetype_surrounding"){
                before_text.clear();context_sampled=false;
                if(active)c->Clear();
                return;
            }
            if(name.rfind("wetype_mode_",0)!=0||!c->get_option(name))return;
            // Rime filters can refresh a translation before this notification,
            // so mode_id may already have changed. The option activation still
            // invalidates the old composition.
            try{choose_mode();c->Clear();}catch(const std::exception &e){error(e);}
        });
    }
    ~Translator(){lifetime.reset();selected.disconnect();deleted.disconnect();updated.disconnect();option_changed.disconnect();if(host)try{
        if(session)host->command("CLOSE "+std::to_string(session));
        for(auto id:fuzzy_sessions)host->command("CLOSE "+std::to_string(id));
    }catch(...){} }
    rime::an<rime::Translation> Query(const string &input,const rime::Segment &segment) override {
        if(!segment.HasTag("abc")||input.empty())return nullptr;
        std::weak_ptr<int> alive=lifetime;
        return std::make_shared<DeferredTranslation>([this,alive,input,start=segment.start,end=segment.end]()->rime::an<rime::Translation>{
            if(alive.expired())return nullptr;
            return snapshot(input,start,end);
        });
    }
};
}
static void rime_wetype_initialize(){
    wetype::install_imk_context();
    rime::Registry::instance().Register("wetype_translator",new rime::Component<wetype::Translator>);
    rime::Registry::instance().Register("wetype_speller",new rime::Component<wetype::ModeSpeller>);
}
static void rime_wetype_finalize(){std::lock_guard<std::mutex> lock(wetype::clients_mu);wetype::clients.clear();}
RIME_REGISTER_MODULE(wetype)
// Tiny loader only. All input/candidate APIs are the official C++ components.
extern "C" __attribute__((visibility("default"))) int rime_wetype_load(void *) {
    // librime's module registry outlives individual Lua states. Keep the
    // native factory/vtables mapped until process exit, including redeploy.
    static void *keepalive=nullptr;
    if(!keepalive){Dl_info info{};if(dladdr(reinterpret_cast<void *>(rime_wetype_load),&info))
        keepalive=dlopen(info.dli_fname,RTLD_NOW|RTLD_GLOBAL|RTLD_NODELETE);}
    rime::ModuleManager::instance().LoadModule(RimeFindModule("wetype"));return 0;
}

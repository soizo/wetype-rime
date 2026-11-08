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
#include "rime_text_context.h"
#include "rime_fuzzy.h"
#include "rime_user_words.h"
#include "rime_lexicon.h"
#include "cell_dicts.h"
#include "session_options.h"
#include "candidate_preferences.h"
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
        // Mixed pinyin can exceed four keys. Only force four-code selection
        // when pinyin assistance is disabled; otherwise keep the composition.
        string pinyin_option;
        bool assisted=!engine_->schema()->config()->GetString("wetype/wubi_pinyin_option",&pinyin_option)
            ||pinyin_option.empty()||ctx->get_option(pinyin_option);
        if(mode.keyboard==5&&!assisted&&!key.release()&&!key.ctrl()&&!key.alt()&&!key.super()&&
           key.keycode()>='a'&&key.keycode()<='z'&&ctx->caret_pos()==ctx->input().size()){
            auto candidate=ctx->GetSelectedCandidate();
            if(candidate&&candidate->end()==ctx->input().size()&&candidate->end()-candidate->start()==4)
                ctx->ConfirmCurrentSelection();
        }
        return speller->ProcessKeyEvent(key);
    }
};
// Keep Rime's table format, lookup, priority and commit handling. This wrapper
// only gates an official table translator with the matching expression option.
class ExpressionTableTranslator:public rime::Translator {
    std::unique_ptr<rime::Translator> table;
    string option;
public:
    explicit ExpressionTableTranslator(const rime::Ticket &ticket):rime::Translator(ticket),
        table(rime::Translator::Require("table_translator")->Create(ticket)){
        engine_->schema()->config()->GetString(ticket.name_space+"/option_name",&option);
    }
    rime::an<rime::Translation> Query(const string &input,const rime::Segment &segment) override{
        return !option.empty()&&engine_->context()->get_option(option)?table->Query(input,segment):nullptr;
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
        :rime::SimpleCandidate("wetype",start,start+row.cover,row.text,row.comment,row.preedit),ticket(revision),session(id),index(row.index),mode(mode_id){}
};
class CandidateActions:public rime::Processor {
public:
    explicit CandidateActions(const rime::Ticket &ticket):rime::Processor(ticket){}
    rime::ProcessResult ProcessKeyEvent(const rime::KeyEvent &key) override{
        auto ctx=engine_->context();
        if(key.release()||ctx->input().empty())return rime::kNoop;
        for(const char *action:{"pin","block","unpin","restore"}){
            string binding;
            if(!engine_->schema()->config()->GetString(string("wetype/candidate_keys/")+action,&binding)||binding.empty())continue;
            if(!(key==rime::KeyEvent(binding)))continue;
            if(!ctx->HasMenu()&&(string(action)=="pin"||string(action)=="block"))return rime::kNoop;
            auto own=std::dynamic_pointer_cast<WetypeCandidate>(rime::Candidate::GetGenuineCandidate(ctx->GetSelectedCandidate()));
            if(!own&&(string(action)=="pin"||string(action)=="block"))return rime::kNoop;
            ctx->set_property("_wetype_candidate_code",own&&(string(action)=="pin"||string(action)=="block")?ctx->input().substr(own->start(),own->end()-own->start()):ctx->input());
            ctx->set_property("_wetype_candidate_text",own?own->text():string());
            ctx->set_property("_wetype_candidate_action",action);
            ctx->set_option("_wetype_candidate_action",!ctx->get_option("_wetype_candidate_action"));
            return rime::kAccepted;
        }
        return rime::kNoop;
    }
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
    unsigned expression_flags=0,wubi_flags=3;
    bool fuzzy_enabled=false;
    RimeFuzzy fuzzy;
    std::vector<uint64_t> fuzzy_sessions;
    RimeUserWords user_words;
    std::shared_future<void> words_preparing;
    bool words_valid=false;
    RimeLexicon lexicon;
    bool lexicon_valid=false;
    std::shared_ptr<const string> dictionary_request;
    std::shared_future<void> lexicon_preparing;
    bool cells_valid=false;
    std::vector<string> cell_ids;
    std::shared_ptr<const string> cells_request;
    std::shared_future<void> cells_preparing;
    string launcher,mode_id;
    SurroundingText text_context;
    ContextSimplifier context_simplifier;
    string logged_context_source;
    bool context_sampled=false;
    std::shared_ptr<int> lifetime=std::make_shared<int>(0);
    bool expression_option(const char *key) const {
        string option;
        return engine_->schema()->config()->GetString(key,&option)
            &&!option.empty()&&engine_->context()->get_option(option);
    }
    unsigned requested_expressions() const {
        return (expression_option("wetype/emoji_option")?WETYPE_UNICODE_EMOJI:0)
            |(expression_option("wetype/kaomoji_option")?WETYPE_KAOMOJI:0)
            |(expression_option("wetype/symbol_emoji_option")?WETYPE_SYMBOL_EMOJI:0);
    }
    bool requested_fuzzy() const {
        string option;
        return engine_->schema()->config()->GetString("wetype/fuzzy_option",&option)
            &&!option.empty()&&engine_->context()->get_option(option);
    }
    void choose_mode(){
        const auto &mode=selected_mode(engine_);
        unsigned expressions=requested_expressions();
        bool use_fuzzy=requested_fuzzy();
        auto enabled=[&](const char *key){
            string option;
            return !engine_->schema()->config()->GetString(key,&option)||option.empty()||engine_->context()->get_option(option);
        };
        unsigned wubi=mode.keyboard==5?((enabled("wetype/wubi_pinyin_option")?1:0)|(enabled("wetype/wubi_comment_option")?2:0)):0;
        if(mode_id==mode.id&&expression_flags==expressions&&fuzzy_enabled==use_fuzzy&&wubi_flags==wubi)return;
        string path;
        auto config=engine_->schema()->config();
        if(!config->GetString(string("wetype/launchers/")+mode.group,&path))
            config->GetString("wetype/launcher",&path);
        if(path.empty())throw std::runtime_error("wetype launcher not configured");
        if(host&&session)host->command("CLOSE "+std::to_string(session));
        if(host)for(auto id:fuzzy_sessions)host->command("CLOSE "+std::to_string(id));
        fuzzy_sessions.clear();
        session=0;active=false;text_context={};context_sampled=false;
        bool changed_host=path!=launcher;
        if(changed_host){host.reset();preparing={};words_preparing={};lexicon_preparing={};cells_preparing={};launcher=path;}
        mode_id=mode.id;
        expression_flags=expressions;wubi_flags=wubi;
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
        if(lexicon_valid&&!lexicon_preparing.valid()){
            auto pending=preparing;
            dictionary_request=Client::lexicon_message(mode.keyboard==5?std::vector<LexiconEntry>{}:lexicon.entries);
            auto request=dictionary_request;
            lexicon_preparing=std::async(std::launch::async,[pending,request]{pending.get()->sync_lexicon(request);}).share();
        }
        if(cells_valid&&!cells_preparing.valid()){
            auto pending=preparing;
            cells_request=Client::cell_message(cell_ids);auto request=cells_request;
            cells_preparing=std::async(std::launch::async,[pending,request]{pending.get()->sync_cells(request);}).share();
        }
        engine_->context()->set_property("_wetype_mode",mode_id);
        engine_->context()->set_property("_wetype_expression_flags",std::to_string(expression_flags));
    }
    void ensure(){
        choose_mode();if(!host)host=preparing.get();
        if(words_preparing.valid()){
            try{words_preparing.get();engine_->context()->set_property("_wetype_user_words_error","");}
            catch(const std::exception &e){engine_->context()->set_property("_wetype_user_words_error",e.what());}
        }
        if(lexicon_preparing.valid()){
            try{lexicon_preparing.get();engine_->context()->set_property("_wetype_dictionary_error","");}
            catch(const std::exception &e){engine_->context()->set_property("_wetype_dictionary_error",e.what());dictionary_request.reset();}
        }
        if(cells_preparing.valid()){
            try{
                cells_preparing.get();engine_->context()->set_property("_wetype_cell_dicts_error","");
                engine_->context()->set_property("_wetype_cell_dicts_count",std::to_string(cell_ids.size()));
            }catch(const std::exception &e){
                engine_->context()->set_property("_wetype_cell_dicts_error",e.what());cells_request.reset();
            }
        }
        if(!session)session=host->open(mode_id,expression_flags,wubi_flags);
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
                    text_context=surrounding_text();
                    ctx->set_property("_wetype_context_source",text_context.available?"imk":"unavailable");
                    if(text_context.available)try{
                        context_simplifier.convert(text_context);
                        ctx->set_property("_wetype_context_error","");
                    }catch(const std::exception &e){
                        text_context={};ctx->set_property("_wetype_context_source","conversion-error");
                        ctx->set_property("_wetype_context_error",e.what());
                    }
                }else{text_context={};ctx->set_property("_wetype_context_source","disabled");}
                ctx->set_property("_wetype_context_bytes",std::to_string(text_context.before.size()));
                ctx->set_property("_wetype_context_after_bytes",std::to_string(text_context.after.size()));
                ctx->set_property("_wetype_context_on_bytes",std::to_string(text_context.on.size()));
                string source=ctx->get_property("_wetype_context_source");
                if(source!=logged_context_source){
                    string path;
                    if(engine_->schema()->config()->GetString("wetype/log_file",&path))
                        if(FILE *f=fopen(path.c_str(),"a")){fprintf(f,"[wetype] surrounding source=%s bytes=%zu\n",source.c_str(),text_context.before.size());fclose(f);}
                    logged_context_source=source;
                }
                context_sampled=true;
            }
            auto result=host->query(session,input,text_context.before,dictionary_request,cells_request,text_context.after,text_context.on);active=true;
            // Promote only exact, full-coverage rows actually returned by the
            // engine. Preserve their original indices for native feedback.
            const bool words_available=words_valid&&ctx->get_property("_wetype_user_words_error").empty();
            auto phrase_priority=[&](const Row &row)->std::optional<int>{
                if(!words_available||row.cover!=input.size())return {};
                return user_words.priority(input,row.text);
            };
            auto configured_phrase=[&](const Row &row,const string &code){
                return words_available&&row.cover<=code.size()&&user_words.contains(code.substr(0,row.cover),row.text);
            };
            std::unique_ptr<CandidatePreferences> preferences;
            try{preferences=std::make_unique<CandidatePreferences>(mode_id);ctx->set_property("_wetype_candidate_error","");}
            catch(const std::exception &e){ctx->set_property("_wetype_candidate_error",e.what());}
            auto pinned=[&](const Row &row){return preferences&&preferences->pinned(input.substr(0,row.cover),row.text);};
            size_t phrases=0;
            for(const auto &row:result.rows)if(phrase_priority(row))++phrases;
            std::stable_sort(result.rows.begin(),result.rows.end(),[&](const Row &a,const Row &b){
                if(pinned(a)!=pinned(b))return pinned(a);
                auto left=phrase_priority(a),right=phrase_priority(b);
                if(bool(left)!=bool(right))return bool(left);
                return left&&right&&*left>*right;
            });
            auto variants=fuzzy_enabled&&ctx->get_property("_wetype_fuzzy_error").empty()?fuzzy.variants(input):std::vector<string>{};
            std::set<std::pair<string,size_t>> seen;
            std::vector<std::pair<bool,std::shared_ptr<WetypeCandidate>>> ordered;
            auto append=[&](const Row &row,uint64_t ticket,uint64_t id,bool is_fuzzy,bool custom_phrase){
                if(preferences&&preferences->blocked(input.substr(0,row.cover),row.text))return;
                if(!variants.empty()&&!seen.insert({row.text,row.cover}).second)return;
                auto candidate=std::make_shared<WetypeCandidate>(start,row,ticket,id,mode_id);
                // Use the same candidate type as Rime's user-table translator,
                // so standard simplifier/excluded_types can preserve literals.
                if(pinned(row))candidate->set_quality(1000);
                if(custom_phrase)candidate->set_type("user_table");
                if(!pinned(row)&&!is_fuzzy&&phrase_priority(row))candidate->set_quality(user_words.initial_quality);
                if(is_fuzzy){if(!custom_phrase)candidate->set_type("wetype_fuzzy");candidate->set_comment("模糊");}
                if(pinned(row))candidate->set_comment(candidate->comment().empty()?"置顶":"置顶 · "+candidate->comment());
                ordered.emplace_back(pinned(row),candidate);
            };
            // Custom phrases precede the first three ordinary exact candidates,
            // followed by bounded fuzzy choices.
            // Each branch owns a separate native session and selection ticket.
            size_t head=variants.empty()?result.rows.size():std::min<size_t>(phrases+3,result.rows.size());
            for(size_t i=0;i<head;++i)append(result.rows[i],result.ticket,session,false,configured_phrase(result.rows[i],input));
            ctx->set_property("_wetype_fuzzy_variants",std::to_string(variants.size()));
            for(size_t i=0;i<variants.size();++i){
                try{
                    if(i>=fuzzy_sessions.size())fuzzy_sessions.push_back(host->open("quanpin",expression_flags));
                    auto alternative=host->query(fuzzy_sessions[i],variants[i],text_context.before,dictionary_request,cells_request,text_context.after,text_context.on);
                    size_t count=0;
                    for(auto row:alternative.rows){
                        // Only complete branch matches are exposed. Partial
                        // selection continues through the untouched native path.
                        if(row.cover!=variants[i].size())continue;
                        bool custom_phrase=configured_phrase(row,variants[i]);
                        row.cover=input.size();row.preedit=input;
                        append(row,alternative.ticket,fuzzy_sessions[i],true,custom_phrase);
                        if(++count==3)break;
                    }
                }catch(const std::exception &e){ctx->set_property("_wetype_fuzzy_error",e.what());}
            }
            for(size_t i=head;i<result.rows.size();++i)append(result.rows[i],result.ticket,session,false,configured_phrase(result.rows[i],input));
            std::stable_sort(ordered.begin(),ordered.end(),[](const auto &a,const auto &b){return a.first>b.first;});
            for(auto &item:ordered)out->Append(item.second);
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
        try{context_simplifier.initialise(engine_);}
        catch(const std::exception &e){ctx->set_property("_wetype_context_error",e.what());}
        try{user_words.load(engine_->schema()->config());words_valid=true;ctx->set_property("_wetype_user_words_error","");}
        catch(const std::exception &e){ctx->set_property("_wetype_user_words_error",e.what());}
        ctx->set_property("_wetype_user_words_skipped",std::to_string(user_words.skipped_codes));
        try{lexicon.load(engine_->schema()->config());lexicon_valid=true;ctx->set_property("_wetype_dictionary_error","");}
        catch(const std::exception &e){ctx->set_property("_wetype_dictionary_error",e.what());}
        ctx->set_property("_wetype_dictionary_entries",std::to_string(lexicon.entries.size()));
        try{
            auto config=engine_->schema()->config();auto item=config->GetItem("wetype/cell_dicts");
            auto list=config->GetList("wetype/cell_dicts");
            if(item&&item->type()!=rime::ConfigItem::kNull&&!list)throw std::runtime_error("wetype/cell_dicts must be a list");
            std::set<string> seen;
            if(list)for(size_t i=0;i<list->size();++i){
                auto value=list->GetValueAt(i);
                if(i>=32||!value||!CellDictionaries::valid_id(value->str())||!seen.insert(value->str()).second)
                    throw std::runtime_error("invalid or duplicate native cell identifier");
                cell_ids.push_back(value->str());
            }
            cells_valid=true;ctx->set_property("_wetype_cell_dicts_error","");
        }catch(const std::exception &e){ctx->set_property("_wetype_cell_dicts_error",e.what());}
        choose_mode();
        Dl_info image{};
        if(dladdr(reinterpret_cast<void *>(&prepare),&image)&&image.dli_fname){
            ctx->set_property("_wetype_adapter",image.dli_fname);
            string log;
            if(engine_->schema()->config()->GetString("wetype/log_file",&log))
                if(FILE *f=fopen(log.c_str(),"a")){
                    fprintf(f,"[wetype] adapter ready: %s\n",image.dli_fname);
                    if(words_valid&&user_words.skipped_codes)fprintf(f,"[wetype] custom_phrase incompatible codes skipped=%zu\n",user_words.skipped_codes);
                    fclose(f);
                }
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
                user_words.weights.erase({c->input().substr(own->start(),own->end()-own->start()),own->text()});
                c->set_property("_wetype_delete_error","");
                c->RefreshNonConfirmedComposition();
            }catch(const std::exception &e){c->set_property("_wetype_delete_error",e.what());}
        });
        updated=ctx->update_notifier().connect(-100,[this](rime::Context *c){
            if(c->input().empty()){
                text_context={};context_sampled=false;c->set_property("_wetype_context_bytes","0");c->set_property("_wetype_context_after_bytes","0");c->set_property("_wetype_context_on_bytes","0");
                if(active&&host){active=false;try{
                    host->command("RESET "+std::to_string(session));
                    for(auto id:fuzzy_sessions)host->command("RESET "+std::to_string(id));
                }catch(const std::exception &e){error(e);}}
            }
        });
        option_changed=ctx->option_update_notifier().connect([this](rime::Context *c,const string &name){
            if(name=="_wetype_candidate_action"){
                auto action=c->get_property("_wetype_candidate_action");
                try{
                    {CandidatePreferences preferences(mode_id);preferences.change(action,c->get_property("_wetype_candidate_code"),c->get_property("_wetype_candidate_text"));}
                    c->set_property("_wetype_candidate_error","");c->RefreshNonConfirmedComposition();
                }catch(const std::exception &e){c->set_property("_wetype_candidate_error",e.what());}
                return;
            }
            string fuzzy_option;
            if(engine_->schema()->config()->GetString("wetype/fuzzy_option",&fuzzy_option)&&name==fuzzy_option){
                try{choose_mode();c->Clear();}catch(const std::exception &e){error(e);}
                return;
            }
            for(const char *key:{"wetype/emoji_option","wetype/kaomoji_option","wetype/symbol_emoji_option","wetype/wubi_pinyin_option","wetype/wubi_comment_option"}){
                string option;
                if(engine_->schema()->config()->GetString(key,&option)&&name==option){
                    try{choose_mode();c->Clear();}catch(const std::exception &e){error(e);}
                    return;
                }
            }
            if(name=="wetype_surrounding"){
                text_context={};context_sampled=false;
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
    rime::Registry::instance().Register("wetype_candidate_actions",new rime::Component<wetype::CandidateActions>);
    rime::Registry::instance().Register("wetype_speller",new rime::Component<wetype::ModeSpeller>);
    rime::Registry::instance().Register("wetype_expression_translator",new rime::Component<wetype::ExpressionTableTranslator>);
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

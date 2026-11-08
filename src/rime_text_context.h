#pragma once
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/gear/simplifier.h>
#include <memory>
#include <stdexcept>
#include "surrounding_text.h"
namespace wetype {
// Use librime's own converter and user/shared OpenCC resource lookup. This
// private conversion is independent of the candidate-output script switch.
class ContextSimplifier {
    std::unique_ptr<rime::Simplifier> converter;
public:
    void initialise(rime::Engine *engine){
        auto config=engine->schema()->config();
        const std::string ns="_wetype_context_t2s";
        auto previous=config->GetItem(ns);
        struct Restore {
            rime::Config *config;std::string key;rime::an<rime::ConfigItem> item;
            ~Restore(){config->SetItem(key,item);}
        } restore{config,ns,previous};
        config->SetItem(ns,std::make_shared<rime::ConfigMap>());
        config->SetString(ns+"/opencc_config","t2s.json");
        rime::SimplifierComponent component;
        converter.reset(component.Create(rime::Ticket(engine,ns)));
        if(!converter)throw std::runtime_error("Rime OpenCC t2s.json unavailable");
    }
    void convert(SurroundingText &context){
        if(!converter)throw std::runtime_error("Rime OpenCC t2s.json unavailable");
        for(auto *field:{&context.before,&context.after,&context.on}){
            if(field->empty())continue;
            auto original=std::make_shared<rime::SimpleCandidate>("wetype_context",0,field->size(),*field);
            rime::CandidateQueue result;
            if(converter->Convert(original,&result)&&!result.empty())*field=result.front()->text();
            // Conversion must not bypass the transport's context bounds.
            if(field->size()>1024||field->find('\0')!=std::string::npos)field->clear();
        }
    }
};
}

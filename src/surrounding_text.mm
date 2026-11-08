// Read bounded editor context through the current IMK client's text-input API.
// The event wrapper forwards the original call unchanged and retains no client.
#import <InputMethodKit/InputMethodKit.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include "surrounding_text.h"
#include <algorithm>
#include <cstring>
namespace {
constexpr const char *bridge_name="WetypeIMKContextBridgeV1";
thread_local __unsafe_unretained id event_client=nil;
using Handler=BOOL (*)(id,SEL,NSEvent *,id);
Handler original_handler=nullptr;
id current_client(id,SEL){return event_client;}
BOOL handle_event(id controller,SEL command,NSEvent *event,id client){
    __unsafe_unretained id previous=event_client;
    event_client=client;
    @try{return original_handler(controller,command,event,client);}
    @finally{event_client=previous;}
}
}
namespace wetype {
bool install_imk_context(){
    // The bridge is process-wide because pinned Rime modules survive reloads.
    // A later module reuses the original wrapper instead of stacking hooks.
    Class bridge=objc_getClass(bridge_name);
    if(bridge)return class_getClassMethod(bridge,sel_registerName("currentClient"))!=nullptr;
    Class controller=objc_getClass("_TtC8Squirrel23SquirrelInputController");
    if(!controller)return false;
    SEL selector=sel_registerName("handleEvent:client:");
    unsigned count=0;Method target=nullptr;
    Method *methods=class_copyMethodList(controller,&count);
    for(unsigned i=0;i<count;++i)if(method_getName(methods[i])==selector)target=methods[i];
    free(methods);
    if(!target||method_getNumberOfArguments(target)!=4)return false;
    char result[8]={};method_getReturnType(target,result,sizeof(result));
    if(std::strcmp(result,"c")&&std::strcmp(result,"B"))return false;
    bridge=objc_allocateClassPair(NSObject.class,bridge_name,0);
    if(!bridge)return false;
    class_addMethod(object_getClass(bridge),sel_registerName("currentClient"),reinterpret_cast<IMP>(current_client),"@@:");
    original_handler=reinterpret_cast<Handler>(method_setImplementation(target,reinterpret_cast<IMP>(handle_event)));
    objc_registerClassPair(bridge);
    return true;
}
SurroundingText surrounding_text(){
    SurroundingText out;
    if(!NSThread.isMainThread||IsSecureEventInputEnabled())return out;
    Class bridge=objc_getClass(bridge_name);
    if(!bridge)return out;
    @try{
        id client=reinterpret_cast<id (*)(id,SEL)>(objc_msgSend)(bridge,sel_registerName("currentClient"));
        if(!client)return out;
        id<IMKTextInput> text_client=client;
        NSRange selection=[text_client selectedRange],marked=[text_client markedRange];
        if(selection.location==NSNotFound)return out;
        // Marked text belongs to this composition, never to its context.
        const bool composing=marked.location!=NSNotFound&&marked.length;
        NSRange excluded=composing?marked:selection;
        if(excluded.length>NSUIntegerMax-excluded.location)return out;
        auto read=[&](NSRange range,bool suffix)->std::string{
            if(!range.length)return {};
            @try{
                NSAttributedString *part=[text_client attributedSubstringFromRange:range];
                if(![part isKindOfClass:NSAttributedString.class])return {};
                NSString *text=part.string;
                if(!text||text.length>range.length+2)return {};
                if(text.length>256){
                    NSUInteger boundary=suffix?text.length-256:256;
                    NSRange character=[text rangeOfComposedCharacterSequenceAtIndex:boundary];
                    if(suffix){if(character.location<boundary)boundary=NSMaxRange(character);text=[text substringFromIndex:boundary];}
                    else{text=[text substringToIndex:character.location];}
                }
                NSData *utf8=[text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
                if(!utf8||utf8.length>1024)return {};
                std::string result(static_cast<const char *>(utf8.bytes),utf8.length);
                return result.find('\0')==std::string::npos?result:std::string();
            }@catch(NSException *){return {};}
        };
        NSUInteger length=std::min<NSUInteger>(excluded.location,256);
        out.before=read(NSMakeRange(excluded.location-length,length),true);
        NSUInteger end=NSMaxRange(excluded);
        out.after=read(NSMakeRange(end,std::min<NSUInteger>(256,NSUIntegerMax-end)),false);
        // A bounded selection is the text under the cursor. Do not substitute
        // marked text, the clipboard or an arbitrary neighbouring character.
        if(!composing&&selection.length<=256)out.on=read(selection,false);
        out.available=true;
    }@catch(NSException *){
        // Unsupported clients must never break keyboard input.
    }
    return out;
}
}

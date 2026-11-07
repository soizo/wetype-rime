// Read the current IMK client's bounded prefix through Apple's text-input API.
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
        // Exclude the whole marked composition, including converted prefixes.
        NSUInteger anchor=marked.location!=NSNotFound&&marked.length?marked.location:selection.location;
        NSUInteger length=std::min<NSUInteger>(anchor,256);
        if(!length){out.available=true;return out;}
        NSAttributedString *part=[text_client attributedSubstringFromRange:NSMakeRange(anchor-length,length)];
        if(![part isKindOfClass:NSAttributedString.class])return out;
        NSString *text=part.string;
        // Clients may adjust a range by a surrogate pair, but must not return
        // an unrelated selection or an unbounded document in its place.
        if(!text||text.length>length+2)return out;
        if(text.length>256){
            NSUInteger start=text.length-256;
            NSRange character=[text rangeOfComposedCharacterSequenceAtIndex:start];
            if(character.location<start)start=NSMaxRange(character);
            text=[text substringFromIndex:start];
        }
        NSData *utf8=[text dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
        if(!utf8||utf8.length>1024)return out;
        out.before.assign(static_cast<const char *>(utf8.bytes),utf8.length);
        if(out.before.find('\0')!=std::string::npos){out.before.clear();return out;}
        out.available=true;
    }@catch(NSException *){
        // Unsupported clients must never break keyboard input.
    }
    return out;
}
}

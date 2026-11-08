#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include "imk_fixture.h"
#include <stdexcept>
@interface WetypeTestTextClient:NSObject
@property(nonatomic,copy) NSString *text;
@property(nonatomic) NSRange selection;
@property(nonatomic) NSRange marked;
@property(nonatomic) bool unavailable;
- (NSRange)selectedRange;
- (NSRange)markedRange;
- (NSAttributedString *)attributedSubstringFromRange:(NSRange)range;
@end
static size_t reads;
@implementation WetypeTestTextClient
- (NSRange)selectedRange{return self.unavailable?NSMakeRange(NSNotFound,0):self.selection;}
- (NSRange)markedRange{return self.marked;}
- (NSAttributedString *)attributedSubstringFromRange:(NSRange)range{
    ++reads;return [[NSAttributedString alloc] initWithString:[self.text substringWithRange:NSIntersectionRange(range,NSMakeRange(0,self.text.length))]];
}
@end
static thread_local void (*callback)(void *);
static thread_local void *callback_context;
static BOOL handle(id,SEL,NSEvent *,id){if(callback)callback(callback_context);return YES;}
void imk_fixture_install(){
    Class c=objc_allocateClassPair(NSObject.class,"_TtC8Squirrel23SquirrelInputController",0);
    if(!c)throw std::runtime_error("test controller class unavailable");
    class_addMethod(c,sel_registerName("handleEvent:client:"),reinterpret_cast<IMP>(handle),"c32@0:8@16@24");
    objc_registerClassPair(c);
}
void imk_fixture_perform(const char *text,size_t selection,size_t marked,size_t length,bool unavailable,
                         void (*action)(void *),void *context,size_t selection_length){
    @autoreleasepool{
        auto previous=callback;auto previous_context=callback_context;
        callback=action;callback_context=context;reads=0;
        WetypeTestTextClient *client=[WetypeTestTextClient new];client.text=[NSString stringWithUTF8String:text];
        client.selection=NSMakeRange(selection,selection_length);client.marked=NSMakeRange(marked==size_t(-1)?NSNotFound:marked,length);client.unavailable=unavailable;
        id controller=[objc_getClass("_TtC8Squirrel23SquirrelInputController") new];
        @try{reinterpret_cast<BOOL (*)(id,SEL,NSEvent *,id)>(objc_msgSend)(controller,sel_registerName("handleEvent:client:"),nil,client);}
        @finally{callback=previous;callback_context=previous_context;}
    }
}
size_t imk_fixture_reads(){return reads;}

#include "mods/mod_actor.h"
#include "mods/story_mail.h"
#include "runtime.h"
#include <cassert>
#include <map>
#include <cstring>
#include <cstdlib>
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {constexpr uint32_t Arena=0x20000000,A=Arena+0x1000,Model=Arena+0x3000,Scratch=Arena+0x4000,Native=Arena+0x5000,Play=Arena+0x10000;
std::map<uint32_t,PpcFunc> registered,hooks;uint32_t next=0xF0000000;unsigned original_calls=0,deleted=0,draws=0,registered_collision=0;bool complete=false,fail_phase=false;uint32_t radius=0,height=0;unsigned heap_getters=0,resource_allocations=0,resource_models=0;uint32_t backing_allocated=0,base_allocated=0;bool allow_talk=false;unsigned message_starts=0,talk_completions=0;uint32_t message_status=0xE;
void map(uint32_t at,uint32_t size){
#ifdef _WIN32
assert(VirtualAlloc(mem::ptr(at),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
assert(mmap(mem::ptr(at),size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void original(Cpu*c,uint32_t fn){assert(fn==0x025E10E0||fn==0x02604550||fn==0x02604EB4||fn==0x02612E64||fn==0x025DD414);++original_calls;
if(fn==0x025DD414){base_allocated=1648;c->r[3]=A;return;}
if(fn==0x02612E64){assert(c->r[6]==Arena+0x9000);backing_allocated=364752;c->r[3]=1;return;}
if(fn==0x02604EB4){Cpu nested=*c;nested.r[3]=Arena+0xB000;nested.r[4]=c->r[4];nested.r[5]=Arena+0x9000;hooks.at(0x02604550)(&nested);c->r[3]=1;return;}c->r[3]=Native;}
uint32_t call(Cpu*c,uint32_t fn,std::initializer_list<uint32_t> args){auto*p=args.begin();switch(fn){case 0x025F7DB0:++message_starts;return 0;case 0x025F74D0:message_status=p[1];return 0;case 0x025F795C:return message_status;case 0x02612504:return 1;case 0x026123FC:assert(p[0]==Arena+0xE000&&p[1]==Arena+0xD000);return Arena+0xE400;case 0x02755FEC:assert(p[1]==Arena+0xE800);return Arena+0x9000;case 0x02604470:case 0x026044E0:assert(p[0]==Arena+0xB000&&p[1]==2&&p[2]==Arena+0x9000);++resource_allocations;st32(p[0]+(fn==0x02604470?0x60:0x64),Arena+0xC000);return 0;case 0x02608308:assert(p[0]==Arena+0xB000&&p[1]==1&&p[2]==1&&p[4]==Arena+0x9000);assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(ld32(p[3]))),"mod_unique.bmd")==0);st32(Arena+0xC000+4,Arena+0x6000);++resource_models;return 0;case 0x025E3268:case 0x025E2FBC:case 0x025EE048:case 0x025EE060:++heap_getters;return Arena+0x9000;case 0xF2000000:assert(p[0]==Arena+0x9000);return 64u*1024*1024-backing_allocated-base_allocated;case 0xF2000004:assert(p[0]==Arena+0x9000||p[0]==Arena+0x7000);return p[0]==Arena+0x7000?10960:13808;case 0x025D4ED0:st8(p[0]+0x3AA,1);return p[0];case 0x02520460:return fail_phase?5:complete?4:1;case 0x026066C4:assert(std::strcmp(reinterpret_cast<char*>(mem::ptr(ld32(p[1]))),"mod_unique")==0);assert(p[2]==1);return Arena+0x6000;case 0x025E38E0:assert(args.size()==2&&p[1]==0x80000);return Model;case 0x025D63E8:{Cpu nested=*c;nested.r[3]=p[0];registered.at(p[1])(&nested);st32(p[0]+0xF4,Arena+0x7000);return nested.r[3];}case 0x02515F14:assert(p[1]==255);return 0;case 0x02516518:assert(p[0]==A+0x3F4);assert(ld32(p[1]+0x18)==0x19);assert(ld32(p[1]+0x04)==0&&ld32(p[1]+0x0C)==0&&ld32(p[1]+0x10)==0&&ld32(p[1]+0x14)==0&&ld32(p[1]+0x2C)==0);return 0;case 0x020184DC:radius=f32_as_u32(float(c->f[1].ps0));return 0;case 0x02018428:height=f32_as_u32(float(c->f[1].ps0));return 0;case 0x025200D4:return Play;case 0x0200E240:assert(p[0]==Play+0x26A4&&p[1]==A+0x3F4);++registered_collision;return 0;case 0x02555D0C:return Arena+0x8000;case 0x025E2DE0:assert(p[0]==Model&&p[1]==0);++draws;return 0;case 0x025204C8:++deleted;return 1;default:return 0;}}
void method(Cpu&c,unsigned n){c.r[3]=A;registered.at(ld32(Scratch+0x30+n*4))(&c);}
}
namespace dispatch {PpcFunc lookup(uint32_t){return nullptr;}uint32_t register_host(PpcFunc fn,const char*){registered[next]=fn;next+=4;return next-4;}}
namespace story_mail {void unsay(){}bool can_say(){return allow_talk;}void say(uint32_t,const std::u16string*){}}
int main(){map(Arena,0x20000);map(0x101F0000,0x10000);map(0x10180000,0x10000);st32(0x1018CA3C,Arena+0x9000);st32(Arena+0x9000+0xC,Arena+0xA000);st32(Arena+0x7000+0xC,Arena+0xA000);st32(Arena+0x7000+0x24,Arena+0x9000);st32(Arena+0xA000+0x74,0xF2000000);st32(Arena+0xA000+0x6C,0xF2000004);st32(0x101F3EE0+0xB6*4,Native);st32(Native+0xC,0x123456);st32(Native+0x1C,0xabcdef);mod_actor::configure(original,call,Scratch);unsigned replacements=0;auto owner=[](uint32_t){return std::string();};auto replace=[&](uint32_t a,PpcFunc f){hooks[a]=f;++replacements;};assert(!mod_actor::install(owner,replace)&&replacements==0);story_notebook::ManifestActor spec;spec.kind="mod";spec.id="post";spec.collision_radius=25;spec.collision_height=80;assert(!mod_actor::declare("test",spec,"",1,{}));assert(!mod_actor::declare("test",spec,"bad_bounds",1,{}, {2,0,0},{1,1,1}));uint32_t token=mod_actor::declare("test",spec,"mod_unique",1,[]{++talk_completions;},{-12,-4,-8},{15,80,10});assert(token==1);assert(mod_actor::install(owner,replace)&&replacements==6);assert(ld32(Scratch+0xC)==0x123456&&ld32(Scratch+0x1C)==0xabcdef);assert(ld16(Scratch+8)==mod_actor::kProfile&&ld32(Scratch+0x10)==0x654&&ld8(Scratch+0x2D)==14&&(ld32(Scratch+0x28)&0x100));Cpu c{};c.r[3]=0xB6;hooks.at(0x025E10E0)(&c);assert(c.r[3]==Native&&original_calls==1);c.r[3]=mod_actor::kProfile;hooks.at(0x025E10E0)(&c);assert(c.r[3]==Scratch&&original_calls==1);st32(Arena+0xD000,Arena+0xD008);std::memcpy(mem::ptr(Arena+0xD008),"retail",7);c.r[3]=Arena+0xB000;c.r[4]=Arena+0xD000;c.r[5]=Arena+0x9000;hooks.at(0x02604550)(&c);assert(original_calls==2&&resource_models==0);
std::memcpy(mem::ptr(Arena+0xD008),"mod_unique",11);c.r[3]=Arena+0xB000;c.r[4]=Arena+0xD000;c.r[5]=Arena+0x9000;hooks.at(0x02604550)(&c);assert(original_calls==2&&resource_allocations==2&&resource_models==1&&c.r[3]==Arena+0xB000);
c.r[3]=Arena+0xE000;c.r[4]=Arena+0xD000;hooks.at(0x02604EB4)(&c);assert(c.r[3]==1&&resource_models==2);
st32(Arena+0xE400+0x14,Arena+0xE800);st32(Arena+0xE400+0x18,392000);c.r[3]=Arena+0xE000;c.r[4]=Arena+0xD000;c.r[6]=Arena+0x9000;hooks.at(0x02612E64)(&c);assert(c.r[3]==1);
st32(Arena+0xF000,token);c.r[3]=mod_actor::kProfile;c.r[5]=Arena+0xF000;hooks.at(0x025DD414)(&c);assert(c.r[3]==A&&ld32(Scratch+512+24)==1648);
st32(Play+0x5B34,Arena+0xF400);st32(A+4,42);st16(A+0xE,mod_actor::kProfile);st32(A+0xB0,token);for(int i=0;i<3;++i)st32(A+0x330+i*4,f32_as_u32(1));method(c,0);assert(c.r[3]==1&&ld8(A+0x3AA)==1&&!ld32(A+0x3B4));complete=true;method(c,0);assert(c.r[3]==4&&ld32(A+0x3B4)==Model);assert(ld32(A+0x3F4+0x44)==A+0x3B8);assert(u32_as_f32(ld32(A+0x34C))==-12&&u32_as_f32(ld32(A+0x350))==-4&&u32_as_f32(ld32(A+0x354))==-8&&u32_as_f32(ld32(A+0x358))==15&&u32_as_f32(ld32(A+0x35C))==80&&u32_as_f32(ld32(A+0x360))==10);method(c,2);assert(c.r[3]==1&&registered_collision==1&&u32_as_f32(radius)==25&&u32_as_f32(height)==80);st32(Play+0x5B34,0);method(c,2);assert(c.r[3]==1&&registered_collision==1);st32(Play+0x5B34,Arena+0xF400);method(c,4);assert(c.r[3]==1&&draws==1);
    // Native ZXY Euler order is RY*RX*RZ. Check actual submitted model matrices, translations and separate scale.
    auto matrix=[&](const float (&expected)[12]){for(unsigned i=0;i<12;++i)assert(std::fabs(u32_as_f32(ld32(Model+0xC8+i*4))-expected[i])<0.00001f);};
    st32(A+0x314,f32_as_u32(12));st32(A+0x318,f32_as_u32(34));st32(A+0x31C,f32_as_u32(56));
    for(unsigned i=0;i<3;++i)st32(A+0x330+i*4,f32_as_u32(float(i+2)));
    st16(A+0x328,0x4000);method(c,2);matrix({1,0,0,12,0,0,-1,34,0,1,0,56});
    st16(A+0x328,0);st16(A+0x32C,0x4000);method(c,2);matrix({0,-1,0,12,1,0,0,34,0,0,1,56});
    st16(A+0x328,0x4000);st16(A+0x32A,0x4000);st16(A+0x32C,0);method(c,2);matrix({0,1,0,12,0,0,-1,34,-1,0,0,56});
    st16(A+0x328,0x2000);st16(A+0x32A,0x1000);st16(A+0x32C,0x3000);method(c,2);
    matrix({0.603553391f,-0.75f,0.270598050f,12,0.653281482f,0.270598050f,-0.707106781f,34,0.457106781f,0.603553391f,0.653281482f,56});
    assert(u32_as_f32(ld32(A+0x34C))==-24&&u32_as_f32(ld32(A+0x350))==-12&&u32_as_f32(ld32(A+0x354))==-32);
    assert(u32_as_f32(ld32(A+0x358))==30&&u32_as_f32(ld32(A+0x35C))==240&&u32_as_f32(ld32(A+0x360))==40);
    assert(ld32(A+0x348)==Model+0xC8); // Native culling applies the unscaled Euler/world matrix exactly once.
    for(unsigned i=0;i<3;++i){assert(u32_as_f32(ld32(Model+0xBC+i*4))==float(i+2));st32(A+0x314+i*4,0);st16(A+0x328+i*2,0);st32(A+0x330+i*4,f32_as_u32(1));}
    method(c,2);
    st32(Play+0x5B34,Arena+0xF400);st32(Arena+0xF400+0x314,f32_as_u32(100));st32(Arena+0xF400+0x31C,f32_as_u32(0));st16(A+0x328,0x2000);st16(A+0x32C,0x1000);st16(A+0xF8,1);method(c,2);assert(ld16(A+0x32A)==0x4000&&ld16(A+0x328)==0x2000&&ld16(A+0x32C)==0x1000);st16(A+0xF8,0);
    // A restored native talk already owns its message process; re-arm its text without spawning twice.
    allow_talk=true;st16(A+0xF8,1);st32(A+0x5E0,1);mod_actor::state_loaded();method(c,2);assert(mod_actor::talking(A)&&message_starts==0);
    st16(A+0xF8,0);method(c,2);assert(!mod_actor::talking(A)&&ld32(A+0x5E0)==0&&talk_completions==0);
    st16(A+0xF8,1);method(c,2);assert(mod_actor::talking(A)&&message_starts==1&&ld32(A+0x5E0)==1);mod_actor::state_loaded();method(c,2);assert(message_starts==1&&mod_actor::talking(A));message_status=0xE;method(c,2);assert(message_status==0x10&&talk_completions==0);message_status=0x11;method(c,2);assert(talk_completions==0);message_status=0x12;method(c,2);assert(talk_completions==1&&!mod_actor::talking(A)&&ld32(A+0x5E0)==2);// Native event reset can lag the completed message; keep it closed across extra steps and a state load.
    method(c,2);mod_actor::state_loaded();method(c,2);assert(message_starts==1&&talk_completions==1&&!mod_actor::talking(A));st16(A+0xF8,0);method(c,2);assert(ld32(A+0x5E0)==0);allow_talk=false;
    mod_actor::state_loaded();method(c,2);method(c,4);assert(registered_collision==18&&draws==2&&ld32(A+0x3B4)==Model);method(c,1);assert(c.r[3]==1&&deleted==1&&ld8(A+0x3AA)==1);assert(std::getenv("WWHD_MOD_ACTOR_HEAPS")?heap_getters>=16:heap_getters==0);st32(Scratch+512+8,2u*1024*1024);method(c,0);assert(c.r[3]==5&&mod_actor::failed(token)&&ld32(Scratch+512)==0);method(c,1);st32(Scratch+512+8,364752);mod_actor::retry(token);
    fail_phase=true;method(c,0);assert(c.r[3]==5&&mod_actor::failed(token));mod_actor::state_loaded();assert(mod_actor::failed(token));mod_actor::retry(token);assert(!mod_actor::failed(token));return 0;}

#include "mod_actor.h"
#include "mod_actor_shadow_proof.h"
#include "mod_actor_budget.h"
#include "story_mail.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <array>
#include <vector>
namespace mod_actor { namespace {
// The extension is guest memory: model, resource phase and collision survive full states.
constexpr uint32_t Phase=0x3AC,Model=0x3B4,Stts=0x3B8,Cyl=0x3F4,Name=0x524,Safe=0x564,Matrix=0x56C,Source=0x59C,Talk=0x5E0,TalkStatus=0x5E4,HeapBaseline=0x630,HeapStarted=0x650,Size=0x654;
struct Decl {std::string mod,archive;story_notebook::ManifestActor spec;int index;std::function<void()> talked;story_notebook::WorldPoint minimum,maximum;};
std::vector<Decl> declarations;
struct ArchiveMeasurement {const Decl* declaration=nullptr;uint32_t heap=0,info=0;};
thread_local ArchiveMeasurement* archive_measurement=nullptr;
uint32_t scratch=0,heap_callback=0;bool installed=false;
void(*original)(Cpu*,uint32_t)=nullptr;
uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
uint32_t talk_actor=0;std::u16string text;
uint32_t call(Cpu*c,uint32_t f,std::initializer_list<uint32_t> a){return invoke(c,f,a);}
uint32_t heap_size(Cpu*c,uint32_t h){if(!h)return 0;uint32_t vt=ld32(h+0xC);if(!vt)return 0;uint32_t fn=ld32(vt+0x6C);return fn?invoke(c,fn,{h}):0;}
uint32_t heap_free(Cpu*c,uint32_t h){if(!h)return 0;uint32_t vt=ld32(h+0xC);if(!vt)return 0;uint32_t fn=ld32(vt+0x74);return fn?invoke(c,fn,{h}):0;}

bool talk_trace(){const char* value=std::getenv("WWHD_MOD_ACTOR_TALK_TRACE");return value&&std::strcmp(value,"1")==0;}
bool heap_trace(){const char* value=std::getenv("WWHD_MOD_ACTOR_HEAPS");return value&&std::strcmp(value,"1")==0;}
using HeapSnapshot=std::array<std::pair<uint32_t,uint32_t>,4>;
HeapSnapshot heaps(Cpu*c){HeapSnapshot out{};const uint32_t getter[]={GC(0x025E3268),GC(0x025E2FBC),GC(0x025EE048),GC(0x025EE060)};for(unsigned i=0;i<4;++i){out[i].first=call(c,getter[i],{});out[i].second=heap_free(c,out[i].first);}return out;}
void report_heaps(uint32_t a,const char* stage,const HeapSnapshot& before,const HeapSnapshot& after){const char* names[]={"archive","game","zelda","command"};for(unsigned i=0;i<4;++i)LOG("[mod_actor_heaps] actor=%08X stage=%s heap=%s pointer=%08X free_before=%u free_after=%u delta=%lld",a,stage,names[i],after[i].first,before[i].second,after[i].second,static_cast<long long>(before[i].second)-after[i].second);}
constexpr uint32_t RecordStart=512,RecordStride=28;
static_assert(RecordStart+128*RecordStride<=kScratchBytes);
uint32_t record(size_t i){return scratch+RecordStart+uint32_t(i)*RecordStride;}
void model_cost(const Decl* own,uint32_t field,uint32_t bytes){for(size_t i=0;i<declarations.size();++i)if(declarations[i].archive==own->archive){const uint32_t at=record(i)+field;st32(at,std::max(ld32(at),bytes));}}
bool check_budget(Cpu*c,uint32_t actor,std::string& why){
    std::vector<budget::Record> records;records.reserve(declarations.size());
    for(size_t i=0;i<declarations.size();++i){const auto& d=declarations[i];const uint32_t r=record(i),a=ld32(r);
        const bool active=a&&ld32(a+4)==ld32(r+4)&&ld16(a+0xE)==kProfile&&ld8(a+0x3AA)==1&&ld32(a+0xB0)==i+1;
        std::memcpy(mem::ptr(scratch+400),d.archive.c_str(),d.archive.size()+1);st32(scratch+472,scratch+400);st32(scratch+476,GD(0x10012504));
        const bool loaded=call(c,GC(0x02612504),{ld32(GD(0x101F4F7C)),scratch+472})!=0;
        records.push_back({d.mod,d.archive,int(d.spec.room),ld32(r+8),ld32(r+12),ld32(r+16),ld32(r+24),loaded,active});
    }
    budget::Usage usage;const int room=int8_t(ld8(actor+0x326));
    const bool ok=budget::check(records,room,usage,why);
    if(heap_trace())LOG("[mod_actor_budget] actor=%08X room=%d game_heap_bytes=%llu limit=%llu accepted=%u",actor,room,static_cast<unsigned long long>(usage.room),static_cast<unsigned long long>(budget::RoomLimit),unsigned(ok));
    return ok;
}
Decl* declaration(uint32_t a){auto t=ld32(a+0xB0);return t&&t<=declarations.size()?&declarations[t-1]:nullptr;}
void result(Cpu*c,uint32_t r){c->r[3]=r;}
void fail_create(uint32_t actor){const uint32_t token=ld32(actor+0xB0);if(scratch&&token&&token<=128)st8(scratch+256+token-1,1);LOG("[mod_actor] create failed actor=%08X token=%u; retry deferred until room reload",actor,token);}
void transform(uint32_t a){
    constexpr float radians=float(3.14159265358979323846/32768.0);
    const float x=float(int16_t(ld16(a+0x328)))*radians;
    const float y=float(int16_t(ld16(a+0x32A)))*radians;
    const float z=float(int16_t(ld16(a+0x32C)))*radians;
    const float cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);
    // Native mDoMtx_ZXYrotS builds RY * RX * RZ (025F1AA4); translation stays in world space.
    const float m[12]={cy*cz+sy*sx*sz,-cy*sz+sy*sx*cz,sy*cx,u32_as_f32(ld32(a+0x314)),
                      cx*sz,cx*cz,-sx,u32_as_f32(ld32(a+0x318)),
                      -sy*cz+cy*sx*sz,sy*sz+cy*sx*cz,cy*cx,u32_as_f32(ld32(a+0x31C))};
    for(int i=0;i<12;++i)st32(a+Matrix+i*4,f32_as_u32(m[i]));
    if(const auto* d=declaration(a)){
        const float minimum[]={d->minimum.x,d->minimum.y,d->minimum.z},maximum[]={d->maximum.x,d->maximum.y,d->maximum.z};
        for(unsigned i=0;i<3;++i){const float scale=u32_as_f32(ld32(a+0x330+i*4));st32(a+0x34C+i*4,f32_as_u32(minimum[i]*scale));st32(a+0x358+i*4,f32_as_u32(maximum[i]*scale));}
    }
    const uint32_t model=ld32(a+Model);
    if(model){for(int i=0;i<3;++i)st32(model+0xBC+i*4,ld32(a+0x330+i*4));for(int i=0;i<12;++i)st32(model+0xC8+i*4,ld32(a+Matrix+i*4));}
}
void heap(Cpu*c){uint32_t a=c->r[3];auto*d=declaration(a);if(!d){result(c,0);return;}st32(a+Safe,a+Name);st32(a+Safe+4,GD(0x10012504));const std::string filename=d->archive+".bmd";uint32_t data=call(c,GC(0x026066C4),{ld32(GD(0x101F4F28)),a+Safe,uint32_t(d->index)});LOG("[mod_actor] %s:%s archive=%s named_resource=%s data=%08X",d->mod.c_str(),d->spec.id.c_str(),d->archive.c_str(),filename.c_str(),data);uint32_t current=call(c,GC(0x027EC230),{}),before=heap_free(c,current);uint32_t model=data?call(c,GC(0x025E38E0),{data,0x80000}):0;uint32_t after=heap_free(c,current);LOG("[mod_actor] %s:%s model heap=%08X free_before=%u free_after=%u model_bytes=%u",d->mod.c_str(),d->spec.model.c_str(),current,before,after,before>=after?before-after:0);st32(a+Model,model);st32(a+0x368,model);st32(a+0x348,model?model+0xC8:0);transform(a);result(c,model!=0);}
void create(Cpu*c){uint32_t a=c->r[3];auto*d=declaration(a);if(!d){result(c,5);return;}if(!ld8(a+0x3AA)){call(c,GC(0x025D4ED0),{a});std::memcpy(mem::ptr(a+Name),d->archive.c_str(),d->archive.size()+1);call(c,GC(0x0200BD2C),{a+Stts});call(c,GC(0x02515DA0),{a+Stts+0x1C});st32(a+Stts+0x18,GD(0x1004AE88));st32(a+Stts+0x1C,GD(0x1004AEC0));call(c,GC(0x02515FB8),{a+Cyl});st32(a+Cyl+0x114,GD(0x100015A8));st32(a+Cyl+0x110,GD(0x1001251C));call(c,GC(0x02018590),{a+Cyl+0x118});st32(a+Cyl+0x3C,GD(0x1004B108));st32(a+Cyl+0x114,GD(0x1004B160));st32(a+Cyl+0x12C,GD(0x1004B150));}
if(heap_trace()&&!ld32(a+HeapStarted)){const auto before=heaps(c);for(unsigned i=0;i<4;++i){st32(a+HeapBaseline+i*8,before[i].first);st32(a+HeapBaseline+i*8+4,before[i].second);}st32(a+HeapStarted,1);}
uint32_t phase=call(c,GC(0x02520460),{a+Phase,a+Name});if(phase!=4){if(phase>=5)fail_create(a);result(c,phase);return;}if(!ld32(a+Model)){
    if(heap_trace()){HeapSnapshot before{};for(unsigned i=0;i<4;++i)before[i]={ld32(a+HeapBaseline+i*8),ld32(a+HeapBaseline+i*8+4)};report_heaps(a,"archive_load",before,heaps(c));}
    const auto before=heap_trace()?heaps(c):HeapSnapshot{};
    if(!call(c,GC(0x025D63E8),{a,heap_callback,0x40000})){fail_create(a);result(c,5);return;}
    if(heap_trace())report_heaps(a,"model_create",before,heaps(c));
}
const uint32_t token=ld32(a+0xB0),account=record(token-1),solid=ld32(a+0xF4),parent=solid?ld32(solid+0x24):0;
if(heap_trace())LOG("[mod_actor_budget] solid=%08X parent=%08X base_bytes=%u",solid,parent,ld32(account+24));
if(!solid||parent<0x10000000u||parent>=0xC0000000u||!ld32(account+8)||!ld32(account+12)||!ld32(account+24)){fail_create(a);LOG("[mod_actor_budget] missing native allocation measurements");result(c,5);return;}
st32(account,a);st32(account+4,ld32(a+4));st32(account+16,heap_size(c,solid));st32(account+20,uint32_t(int8_t(ld8(a+0x326))));
std::string budget_error;
if(heap_free(c,parent)<budget::SolidReserve||!check_budget(c,a,budget_error)){st32(account,0);fail_create(a);LOG("[mod_actor_budget] rejected %s:%s (%s)",d->mod.c_str(),d->spec.id.c_str(),budget_error.empty()?"solid parent reserve below 1 MiB":budget_error.c_str());result(c,5);return;}
call(c,GC(0x02515F14),{a+Stts,d->spec.pushable?100u:255u,0,a});for(unsigned i=0;i<0x44;i+=4)st32(a+Source+i,0);st32(a+Source+0x18,0x19);call(c,GC(0x02516518),{a+Cyl,a+Source});st32(a+Cyl+0x44,a+Stts);st8(a+0x389,5);st8(a+0x38B,6);st32(a+0x39C,0xA|(d->spec.prompt=="check"?0x20000000u:0u));if(heap_trace())LOG("[mod_actor] ready actor=%08X model=%08X pos=(%.2f,%.2f,%.2f) scale=(%.2f,%.2f,%.2f) cull=%u",a,ld32(a+Model),double(u32_as_f32(ld32(a+0x314))),double(u32_as_f32(ld32(a+0x318))),double(u32_as_f32(ld32(a+0x31C))),double(u32_as_f32(ld32(a+0x330))),double(u32_as_f32(ld32(a+0x334))),double(u32_as_f32(ld32(a+0x338))),unsigned(ld8(a+0x2DB)));result(c,4);}
void destroy(Cpu*c){uint32_t a=c->r[3];mod_actor_shadow_proof::forget_model(ld32(a+Model));const uint32_t token=ld32(a+0xB0);if(token&&token<=declarations.size()&&ld32(record(token-1))==a)st32(record(token-1),0);const auto before=heap_trace()?heaps(c):HeapSnapshot{};if(talk_actor==a){story_mail::unsay();talk_actor=0;text.clear();}call(c,GC(0x02515A70),{a+Cyl,2});call(c,GC(0x02515860),{a+Stts,2});call(c,GC(0x025204C8),{a+Phase,a+Name});if(heap_trace())report_heaps(a,"resource_delete",before,heaps(c));result(c,1);}
void is_delete(Cpu*c){result(c,1);}
void execute(Cpu*c){uint32_t a=c->r[3];auto*d=declaration(a);if(!d){result(c,1);return;}uint32_t play=call(c,GC(0x025200D4),{});if(!play||!ld32(play+0x5B34)){result(c,1);return;}if(d->spec.pushable)for(unsigned i=0;i<3;++i)st32(a+0x314+i*4,f32_as_u32(u32_as_f32(ld32(a+0x314+i*4))+u32_as_f32(ld32(a+Stts+i*4))));call(c,GC(0x02515E50),{a+Stts+0x1C});for(unsigned i=0;i<3;++i){float v=u32_as_f32(ld32(a+0x314+i*4))+(i==1?d->spec.collision_height*0.7f:0);st32(a+0x37C+i*4,f32_as_u32(v));st32(a+0x390+i*4,f32_as_u32(v));}call(c,GC(0x020182E0),{a+Cyl+0x118,a+0x314});c->f[1].ps0=d->spec.collision_radius;call(c,GC(0x020184DC),{a+Cyl+0x118});c->f[1].ps0=d->spec.collision_height;call(c,GC(0x02018428),{a+Cyl+0x118});call(c,GC(0x0200E240),{play+0x26A4,a+Cyl});
if(ld16(a+0xF8)==1){
    const uint32_t player=ld32(play+0x5B34);
    if(player){const float dx=u32_as_f32(ld32(player+0x314))-u32_as_f32(ld32(a+0x314)),dz=u32_as_f32(ld32(player+0x31C))-u32_as_f32(ld32(a+0x31C));st16(a+0x32A,uint16_t(int32_t(std::atan2(dx,dz)*32768.0/3.14159265358979323846)));}
    // Guest talk state is independent of native message IDs: zero is a successful messageSet result.
    // 0 = inactive, 1 = native message started, 2 = closed, waiting for event reset.
    if(!talk_actor&&ld32(a+Talk)!=2&&story_mail::can_say()){
        const bool resumed=ld32(a+Talk)==1;talk_actor=a;
        story_mail::append_utf16(d->spec.lines,true,text);story_mail::say(0x2CF3,&text);
        uint32_t message=0;
        if(!resumed){message=call(c,GC(0x025F7DB0),{ld32(GD(0x101F4B5C)),0x2CF3,a+0x37C});
            if(message!=0xFFFFFFFFu)st32(a+Talk,1);
            else {story_mail::unsay();talk_actor=0;text.clear();}}
        if(talk_trace())LOG("[mod_actor_talk] %s actor=%08X message=%08X state=%u",resumed?"resume":"start",a,message,ld32(a+Talk));
    }else if(talk_actor==a){
        const uint32_t status=call(c,GC(0x025F795C),{ld32(GD(0x101F4B5C))});
        if(talk_trace()&&ld32(a+TalkStatus)!=status){LOG("[mod_actor_talk] status actor=%08X state=%u status=%u",a,ld32(a+Talk),status);st32(a+TalkStatus,status);}
        if(status==0xE){
            // Native NPC next_msgStatus chooses ENDS at DISPLAYED; the native window then handles A.
            call(c,GC(0x025F74D0),{ld32(GD(0x101F4B5C)),0x10});
        }else if(status==0x12){
            call(c,GC(0x025F74D0),{ld32(GD(0x101F4B5C)),0x13});
            st16(play+0x51D0+0xE8,ld16(play+0x51D0+0xE8)|8);
            story_mail::unsay();talk_actor=0;text.clear();st32(a+Talk,2);
            if(talk_trace())LOG("[mod_actor_talk] finish actor=%08X command=%u event_flags=%04X",a,unsigned(ld16(a+0xF8)),unsigned(ld16(play+0x51D0+0xE8)));
            if(d->talked)d->talked();
        }
    }
}else {
    if(talk_actor==a){if(talk_trace())LOG("[mod_actor_talk] cancel actor=%08X",a);story_mail::unsay();talk_actor=0;text.clear();}
    st32(a+Talk,0);st32(a+TalkStatus,0);st16(a+0xFA,ld16(a+0xFA)|1);
}
transform(a);result(c,1);}
void draw(Cpu*c){uint32_t a=c->r[3],m=ld32(a+Model);if(m){uint32_t env=call(c,GC(0x02555D0C),{});call(c,GC(0x025626A4),{env,0,a+0x314,a+0x110});call(c,GC(0x02562F5C),{env,m,a+0x110});mod_actor_shadow_proof::begin_model(m);call(c,GC(0x025E2DE0),{m,0});mod_actor_shadow_proof::end_model();}result(c,1);}
void resource_table(Cpu*c){
    const uint32_t info=c->r[3],key=c->r[4],heap=c->r[5];
    const uint32_t chars=key?ld32(key):0;
    const Decl* found=nullptr;
    if(chars)for(const auto& d:declarations)if(std::strncmp(reinterpret_cast<const char*>(mem::ptr(chars)),d.archive.c_str(),64)==0){found=&d;break;}
    if(!found){original(c,GC(0x02604550));return;}
    const Cpu incoming=*c;
    if(archive_measurement&&archive_measurement->declaration==found){archive_measurement->heap=heap;archive_measurement->info=info;}
    const uint32_t before=heap_trace()?heap_free(c,heap):0;
    const std::string filename=found->archive+".bmd";
    std::memcpy(mem::ptr(scratch+128),filename.c_str(),filename.size()+1);
    st32(scratch+200,scratch+128);st32(scratch+204,GD(0x10012504));
    const uint32_t count=uint32_t(found->index)+1;
    call(c,GC(0x02604470),{info,count,heap});call(c,GC(0x026044E0),{info,count,heap});
    const uint32_t model_before=heap_trace()?heap_free(c,heap):0;
    call(c,GC(0x02608308),{info,uint32_t(found->index),uint32_t(found->index),scratch+200,heap});
    if(heap_trace()){const uint32_t after=heap_free(c,heap);LOG("[mod_actor_heaps] archive=%s stage=resource_model heap=%08X free_before=%u free_after=%u delta=%lld",found->archive.c_str(),heap,model_before,after,static_cast<long long>(model_before)-after);}
    if(heap_trace()){const uint32_t after=heap_free(c,heap);LOG("[mod_actor_heaps] archive=%s stage=resource_table heap=%08X free_before=%u free_after=%u delta=%lld",found->archive.c_str(),heap,before,after,static_cast<long long>(before)-after);}
    LOG("[mod_actor] native archive table %s index=%d model=%08X",found->archive.c_str(),found->index,ld32(info+0x60)?ld32(ld32(info+0x60)+uint32_t(found->index)*4):0);
    *c=incoming;
}
void backing_archive_wrapper(Cpu*c){
    const uint32_t manager=c->r[3],key=c->r[4],allocation_heap=c->r[6],chars=key?ld32(key):0;const Decl* own=nullptr;
    if(chars)for(const auto& d:declarations)if(std::strncmp(reinterpret_cast<const char*>(mem::ptr(chars)),d.archive.c_str(),64)==0){own=&d;break;}
    if(!own){original(c,GC(0x02612E64));return;}
    const Cpu incoming=*c;const uint32_t before=heap_free(c,allocation_heap);
    const bool cached=call(c,GC(0x02612504),{manager,key})!=0;
    if(!cached&&before<budget::BackingReserve+budget::ModelLimit){*c=incoming;result(c,0);LOG("[mod_actor_budget] backing heap reserve prevents loading %s",own->archive.c_str());return;}
    *c=incoming;
    original(c,GC(0x02612E64));const Cpu returned=*c;
    const uint32_t after=heap_free(c,allocation_heap);
    uint32_t resource=0,buffer=0,bytes=0,owner=0;
    if(returned.r[3]){resource=call(c,GC(0x026123FC),{manager,key});if(resource){buffer=ld32(resource+0x14);bytes=ld32(resource+0x18);if(buffer)owner=call(c,GC(0x02755FEC),{ld32(GD(0x101F8B4C)),buffer});}}
    if(returned.r[3]&&before>after)model_cost(own,8,before-after);
    if(heap_trace())LOG("[mod_actor_heaps] archive=%s stage=archive_backing heap=%08X free_before=%u free_after=%u delta=%lld resource=%08X buffer=%08X buffer_bytes=%u buffer_owner=%08X",own->archive.c_str(),allocation_heap,before,after,static_cast<long long>(before)-after,resource,buffer,bytes,owner);
    *c=returned;
}
void archive_wrapper(Cpu*c){
    const uint32_t key=c->r[4],chars=key?ld32(key):0;const Decl* own=nullptr;
    if(chars)for(const auto& d:declarations)if(std::strncmp(reinterpret_cast<const char*>(mem::ptr(chars)),d.archive.c_str(),64)==0){own=&d;break;}
    if(!own){original(c,GC(0x02604EB4));return;}
    ArchiveMeasurement current{own,0,0};ArchiveMeasurement* previous=archive_measurement;archive_measurement=&current;
    try{original(c,GC(0x02604EB4));}catch(...){archive_measurement=previous;throw;}
    archive_measurement=previous;const Cpu returned=*c;
    if(returned.r[3]&&current.heap){const uint32_t size=heap_size(c,current.heap),free=heap_free(c,current.heap);model_cost(own,12,size);if(heap_trace())LOG("[mod_actor_heaps] archive=%s stage=archive_adjusted heap=%08X info=%08X size=%u free=%u used=%u",own->archive.c_str(),current.heap,current.info,size,free,size>=free?size-free:0);}
    *c=returned;
}
void delete_wrapper(Cpu*c){const uint32_t actor=c->r[3];const bool own=actor&&ld16(actor+0xE)==kProfile&&heap_trace();const auto before=own?heaps(c):HeapSnapshot{};c->r[3]=actor;original(c,GC(0x025D4B4C));const Cpu returned=*c;if(own)report_heaps(actor,"actor_delete",before,heaps(c));*c=returned;}
void base_create_wrapper(Cpu*c){
    if(c->r[3]!=kProfile){original(c,GC(0x025DD414));return;}
    const Cpu incoming=*c;const uint32_t parameters=c->r[5],token=parameters?ld32(parameters):0;
    const uint32_t allocation_heap=ld32(GD(0x1018CA3C)),before=heap_free(c,allocation_heap);*c=incoming;
    original(c,GC(0x025DD414));const Cpu returned=*c;const uint32_t after=heap_free(c,allocation_heap);
    if(returned.r[3]&&token&&token<=declarations.size()){st32(record(token-1)+24,before>=after?before-after:0);if(heap_trace())LOG("[mod_actor_heaps] actor=%08X stage=actor_base heap=%08X free_before=%u free_after=%u delta=%u",returned.r[3],allocation_heap,before,after,before>=after?before-after:0);}
    *c=returned;
}
void profile(Cpu*c){if(c->r[3]==kProfile)result(c,scratch);else original(c,GC(0x025E10E0));}
}
void configure(void(*o)(Cpu*,uint32_t),uint32_t(*i)(Cpu*,uint32_t,std::initializer_list<uint32_t>),uint32_t s){original=o;invoke=i;scratch=s;}
uint32_t declare(const std::string&mod,const story_notebook::ManifestActor&spec,const std::string&archive,int index,std::function<void()> talked,story_notebook::WorldPoint minimum,story_notebook::WorldPoint maximum){const float lo[]={minimum.x,minimum.y,minimum.z},hi[]={maximum.x,maximum.y,maximum.z};for(unsigned i=0;i<3;++i)if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])return 0;if(archive.empty()||archive.size()>63||index<0||index>16||declarations.size()>=128)return 0;declarations.push_back({mod,archive,spec,index,std::move(talked),minimum,maximum});return uint32_t(declarations.size());}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if(declarations.empty()||!scratch||!invoke||!original||!owner(GC(0x025E10E0)).empty()||!owner(GC(0x025D4B4C)).empty()||!owner(GC(0x02604550)).empty()||!owner(GC(0x02604EB4)).empty()||!owner(GC(0x02612E64)).empty()||!owner(GC(0x025DD414)).empty())return false;std::memset(mem::ptr(scratch+256),0,kScratchBytes-256);uint32_t native=ld32(GD(0x101F3EE0)+0xB6*4);if(!native)return false;for(unsigned i=0;i<0x30;i+=4)st32(scratch+i,ld32(native+i));st16(scratch+8,kProfile);st32(scratch+0x10,Size);st32(scratch+0x14,0);st32(scratch+0x18,0);st32(scratch+0x24,scratch+0x30);st32(scratch+0x28,0x44100);st8(scratch+0x2C,0);st8(scratch+0x2D,14);void(*methods[])(Cpu*)={create,destroy,execute,is_delete,draw};for(unsigned i=0;i<5;++i)st32(scratch+0x30+i*4,dispatch::register_host(methods[i],"mod_actor_method"));heap_callback=dispatch::register_host(heap,"mod_actor_heap");replace(GC(0x025E10E0),profile);replace(GC(0x025D4B4C),delete_wrapper);replace(GC(0x02604550),resource_table);replace(GC(0x02604EB4),archive_wrapper);replace(GC(0x02612E64),backing_archive_wrapper);replace(GC(0x025DD414),base_create_wrapper);installed=true;return true;}
void state_loaded(){mod_actor_shadow_proof::state_loaded();if(talk_actor)story_mail::unsay();talk_actor=0;text.clear();}
bool talking(uint32_t actor){return actor&&talk_actor==actor;}
bool failed(uint32_t token){return scratch&&token&&token<=128&&ld8(scratch+256+token-1)!=0;}
void retry(uint32_t token){if(scratch&&token&&token<=128)st8(scratch+256+token-1,0);}
}

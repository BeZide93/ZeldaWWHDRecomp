// SPDX-License-Identifier: MPL-2.0
// Synthetic native boundary tests; no game physics/render acceptance claim.
#include "mods/native_actor_refusal.h"
#include "mods/story_enemies.h"
#include "mods/native_actor_profiles.h"
#include "runtime.h"
#include "guest_addr.h"
#ifndef STORY_ENEMIES_TEST_CODE_DELTA
#define STORY_ENEMIES_TEST_CODE_DELTA 0u
#endif
#include <cassert>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*,...){}
namespace {
constexpr uint32_t Actor=0x20000000,Second=Actor+0x2000,Retail=Actor+0x4000,Arena=0x60000000,Bokoblin=Actor+0x6000,Moblin=Actor+0xA000;
std::map<uint32_t,void(*)(Cpu*)> hooks;std::map<uint32_t,bool> defeated;
unsigned calls=0,claims=0,creates=0,deletes=0,drops=0,marker_sequence=0;bool terminal=false,freeze_terminal=false,fail_claim=false,throw_execute=false,children_ready=false;unsigned child_scopes=0,child_retires=0;unsigned tn_execs=0,tn_nested=0,kid_deletes=0;bool drown_mode=false,bad_helper=false,bad_type=false,foreign_helper=false,request_ok=true,request_queued=true,throw_create=false,nested_weapon=false;uint32_t drown_lr=0x02044FCC;
void map_memory(uint32_t a,uint32_t n){
#ifdef _WIN32
 assert(VirtualAlloc(mem::ptr(a),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
#else
 assert(mmap(mem::ptr(a),n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)!=MAP_FAILED);
#endif
}
void call_hook(Cpu*c,uint32_t f,uint32_t a){Cpu nested=*c;nested.r[3]=a;hooks.at(f)(&nested);}
bool known_address(uint32_t fn){const uint32_t known[]={0x02110D4C,0x0210DD3C,0x0210BFEC,0x021104B4,0x0219D568,0x0219AAE8,0x02199748,0x0219CE98,0x0209F918,0x0209BD1C,0x0209971C,0x0209F164,0x021CCB00,0x021C9B1C,0x021C7344,0x021CC44C,0x02446E54,0x02446374,0x02445EE4,0x024468E4,0x023CF600,0x023CC070,0x023C9994,0x023CEF04,0x0230F32C,0x0230BEF4,0x0230AE30,0x0230EDD0,0x0231175C,0x02311240,0x023111D8,0x023115BC,0x024C0B0C,0x024BD170,0x024BA238,0x024C0194,0x025D99E8,0x020402C8,0x025D9154,0x021992DC,0x025D9898,0x021C9AA0,0x025D7B24,0x020A47B4,0x021D0294,0x020A3B18,0x02041570,0x02041F94,0x02043F34,0x025D57E0,0x020BFC50,0x020BCEC8,0x020BBAEC,0x020BF574,0x020ADA5C,0x020AB020,0x020A8BDC,0x020AD59C,0x023C3EC8,0x023C1380,0x023C039C,0x023C38AC};bool accepted=false;for(uint32_t expected:known)accepted|=fn==expected;for(const auto&n:story_enemies_more::kNatives)accepted|=fn==n.create||fn==n.execute||fn==n.draw||fn==n.destroy;return accepted;}
std::function<bool(Cpu*,uint32_t)> probe;  // batch-3 kinds: a test's stand-in for the native step
void original(Cpu*c,uint32_t fn){fn-=STORY_ENEMIES_TEST_CODE_DELTA;assert(known_address(fn));++calls;const uint32_t a=c->r[3];if(probe&&probe(c,fn))return;

 if(fn==0x02110D4C||fn==0x0219D568||fn==0x0209F918||fn==0x021CCB00){if(fn==0x0209F918||fn==0x021CCB00){const uint32_t flag=GD(fn==0x0209F918?0x10191468:0x101BA690);if(a!=Retail)assert(ld8(flag)==1);else assert(ld8(flag)==0);if(throw_create)throw std::runtime_error("synthetic create unwind");}++creates;c->r[3]=4;return;}
 // Darknut: its create sets the HIO latch only when it may own it, rewrites the shared search_sp/s_check
 // statics and runs its own execute three times; its execute fills the shared weapon search scratch
 if(fn==0x024C0B0C){assert(ld8(GD(0x101D2468))==1);st8(GD(0x1046E73C),1);st8(GD(0x1046E73D),0);const unsigned before=tn_execs;for(int i=0;i<3;++i)call_hook(c,0x024BD170,a);tn_nested+=tn_execs-before;++creates;c->r[3]=4;return;}
 if(fn==0x024BD170){++tn_execs;st32(GD(0x1046E718),5);for(unsigned i=0;i<10;++i)st32(GD(0x1046E740)+4*i,0x200+i);if(terminal){st16(a+0x596,20);call_hook(c,0x025D99E8,a);}c->r[3]=1;return;}
 if(fn==0x024C0194){++deletes;return;}
 if(fn==0x021104B4||fn==0x0219CE98||fn==0x0209F164||fn==0x021CC44C){++deletes;return;}
 if(fn==0x025D57E0){if(ld16(a+0xE)==0x1CF||ld16(a+0xE)==0xC0)++kid_deletes;if(request_ok&&request_queued)st8(a+0xC,3);c->r[3]=request_ok?1:0;return;}
 if(fn==0x02043F34){Cpu request=*c;request.r[3]=ld32(a);request.lr=GC(drown_lr);hooks.at(0x025D57E0)(&request);c->r[3]=0x11223344;return;}
 if(fn==0x025D99E8||fn==0x025D9154){++drops;c->r[3]=123;return;}
 if(fn==0x0210DD3C||fn==0x0219AAE8||fn==0x0209BD1C||fn==0x021C9B1C){st32(GD(0x10462CF0),1234);st32(GD(0x10464B28),9876);
  if(fn==0x0209BD1C||fn==0x021C9B1C){const bool bk=fn==0x0209BD1C;st16(GD(0x10462474),bk?0x1111:0x2222);const uint32_t count=GD(bk?0x10462320:0x10465498),items=GD(bk?0x10462330:0x104654A8);st32(count,44);for(unsigned i=0;i<10;++i)st32(items+4*i,100+i);if(bk){st32(GD(0x1046231C),333);st32(GD(0x1046232C),0xAABBCCDD);}else{st8(GD(0x104654A4),66);st8(GD(0x101BA692),77);for(uint32_t off:{0x28u,0x34u,0x3Cu,0x44u,0x48u})st32(GD(0x104654E0)+off,0xABC00000u+off);}}
  if(throw_execute)throw std::runtime_error("synthetic native unwind");
  if(fn==0x0209BD1C&&nested_weapon){call_hook(c,0x021C9B1C,Moblin);assert(ld16(GD(0x10462474))==0x1111);}
  if(fn==0x0219AAE8){st32(GD(0x10464B28),0);call_hook(c,0x021992DC,Second);assert(ld32(GD(0x10464B28))==1);call_hook(c,0x021992DC,Retail);assert(ld32(GD(0x10464B28))==1);}
  if(drown_mode&&a==Bokoblin){const uint32_t h=foreign_helper?Retail+0x49C:bad_helper?a+0x500:a+0x49C;st32(h,foreign_helper?Retail:a);st16(h+8,bad_type?1:2);call_hook(c,0x02043F34,h);}
  if(terminal){if(a==Bokoblin)st16(a+0x49C+6,20);else if(a==Moblin)st16(a+0x8BC+6,20);else{st8(a+0x3FC,4);st8(a+0x3FD,51);st8(a+0x489,30);}call_hook(c,0x025D99E8,a);}
  if(freeze_terminal)call_hook(c,0x020402C8,a+(a==Actor?0x924:0xAD8));
  c->r[3]=1;return;
 }
 if(fn==0x020402C8){const uint32_t owner=ld32(a);Cpu item=*c;item.r[3]=owner+0x314;item.r[4]=ld32(owner+0x3A4);item.r[6]=owner+0x320;hooks.at(0x025D9154)(&item);st32(owner+0x39C,ld32(owner+0x39C)|0x10);c->r[3]=1;return;}
 c->r[3]=1;
}
unsigned base_constructs=0;
uint32_t invoke(Cpu*c,uint32_t fn,std::initializer_list<uint32_t>args){if(fn==GC(0x025D4ED0)){const uint32_t a=*args.begin();assert(args.size()==1&&ld8(a+0x3AA)==0);st8(a+0x3AA,1);st32(a+0xB4,GD(0x10057138));++base_constructs;return a;}assert(fn==GC(0x025D57E0)&&args.size()==1);c->r[3]=*args.begin();original(c,fn);c->r[4]=999;c->pc=999;return c->r[3];}
void actor(uint32_t a,uint32_t pid,story_enemies::Kind k){const auto&d=story_enemy_ownership::descriptor(k);std::memset(mem::ptr(a),0,d.bytes);st32(a+4,pid);st16(a+0xE,d.profile);st32(a+0xB4,GD(k==story_enemies::Kind::ChuChu?0x1000C5A0:k==story_enemies::Kind::Bokoblin?0x1000957C:k==story_enemies::Kind::Moblin?0x10014E9C:k==story_enemies::Kind::Darknut?0x10040490:0x10012C24));st32(a+0xB0,story_enemies::parameters(k));st8(a+0x2DD,0xFF);st16(a+0x2D8,0xFFFF);st16(a+0x2FC,story_enemies::home_z(k));
 if(k==story_enemies::Kind::ChuChu){st8(a+0x3FB,0xFF);st32(a+0x924,a);}else if(k==story_enemies::Kind::Bokoblin){st8(a+0x3D0,4);st8(a+0x3D1,0xFF);st8(a+0x3D2,0xFF);st8(a+0x3D3,0xFF);st32(a+0x49C,a);st16(a+0x49C+8,2);st32(a+0x13BC,a);}else if(k==story_enemies::Kind::Moblin){st8(a+0x3DA,0xFF);st8(a+0x3DB,0xFF);st32(a+0x8BC,a);st16(a+0x8BC+8,1);st32(a+0x2E54,a);}else if(k==story_enemies::Kind::Darknut){const uint32_t p=ld32(a+0xB0);st8(a+0x3D8,4);st8(a+0x3D9,uint8_t(p>>8));st8(a+0x3DA,0xFF);st8(a+0x3DB,0xFF);st8(a+0x4DC,uint8_t((p>>4)&0xF));st32(a+0x590,a);st16(a+0x590+8,3);st32(a+0x1674,a);}else if(k==story_enemies::Kind::Rat){st32(a+0xB4,GD(0x1002442C));st8(a+0x3D0,1);st32(a+0xBE0,a);st32(a+0xF98,a);}else if(k==story_enemies::Kind::RatHole){st32(a+0xB4,GD(0x10024598));st8(a+0x3D5,1);st8(a+0x3D6,1);st8(a+0x405,0xFF);}else if(k==story_enemies::Kind::Peahat){st32(a+0xB4,GD(0x10034494));st32(a+0xAB4,a);st32(a+0xE6C,a);st32(a+0x1098,a);}else if(k==story_enemies::Kind::BokoBaba){st32(a+0xB4,GD(0x1000A184));}else if(k==story_enemies::Kind::Bubble){st32(a+0xB4,GD(0x10009964));st8(a+0x3EC,uint8_t(ld32(a+0xB0)&0xFF));st8(a+0x3F1,0xFF);st8(a+0x405,0xFF);}else if(k==story_enemies::Kind::Octorok){st32(a+0xB4,GD(0x10033E10));}else if(k==story_enemies::Kind::Miniblin){st32(a+0xB4,GD(0x10038234));st8(a+0x3D0,1);st8(a+0x3D1,10);st8(a+0x3D3,0xFF);st8(a+0x3D4,0xFF);st32(a+0x8FC,a);}else{st8(a+0x486,0xFF);st8(a+0x487,0xFF);st32(a+0xAD8,a);}
 for(size_t i=0;i<d.collider_count;++i){const auto&x=d.colliders[i];st32(a+x.object+0x44,a+x.stts);st32(a+x.stts+0xC,a);st32(a+x.stts+0x10,pid);if(x.helper)st32(a+x.helper,a);}
}
const std::pair<uint32_t,size_t> scratch_ranges[]={{0x10462320,4},{0x10462330,40},{0x1046231C,4},{0x1046232C,4},{0x10465498,4},{0x104654A8,40},{0x104654A4,1},{0x101BA692,1},{0x104654E0+0x28,0x24},{0x10462474,2}};
std::vector<uint8_t> weapon_snapshot(){std::vector<uint8_t> out;for(auto range:scratch_ranges){const auto*p=static_cast<const uint8_t*>(mem::ptr(GD(range.first)));out.insert(out.end(),p,p+range.second);}return out;}
void initialize_weapon_scratch(){for(auto range:scratch_ranges)std::memset(mem::ptr(GD(range.first)),0x5A,range.second);st16(GD(0x10462474),0x7777);}
void run(uint32_t fn,uint32_t a){Cpu c{};c.r[3]=a;c.r[4]=0x12345678;c.f[2].ps0=42;hooks.at(fn)(&c);assert(c.r[4]==0x12345678&&c.f[2].ps0==42);}
}
namespace native_actor_profiles {
void configure(void(*)(Cpu*,uint32_t),uint32_t(*)(Cpu*,uint32_t,std::initializer_list<uint32_t>)){}
bool valid_storage(uint32_t a){return a>=Arena&&a<Arena+0x20000;}
bool available(const Owner&){return true;}bool add(uint16_t,uint32_t,BaseAllocated){return true;}bool install(const Owner&,const Replace&){return true;}
}
namespace story_items {
std::optional<Item> find(const std::string&,const std::string&){return std::nullopt;}
EnemyMarkerHandle register_enemy_marker(const std::string&,const std::string&,const std::string&,std::string&){return EnemyMarkerHandle{++marker_sequence};}
bool forget_prop_marker(PropMarkerHandle){return true;}
bool enemy_defeated(EnemyMarkerHandle h){return defeated[h.value];}
EnemyClaimStatus claim_enemy_drops(EnemyMarkerHandle h,const std::string&,const std::vector<PropDrop>&,bool){++claims;if(fail_claim)return EnemyClaimStatus::InvalidDefinition;if(defeated[h.value])return EnemyClaimStatus::AlreadyDefeated;defeated[h.value]=true;return EnemyClaimStatus::Committed;}
}
// Batch-3 enemies: each test keeps the claim counter as it found it.
constexpr uint32_t B3=0x20020000;  // the batch-3 test actors: a region of their own
constexpr uint32_t More=0x20050000;  // the table kinds' test actors (story_enemies_more.h): a region of their own
struct KeepClaims{unsigned v=claims;~KeepClaims(){claims=v;}};
void toggles(story_enemies::Token t,story_enemies::Kind k,uint32_t a,std::initializer_list<uint32_t> offsets){for(uint32_t off:offsets){const uint8_t old=ld8(a+off);st8(a+off,uint8_t(old^1));assert(!story_enemies::verify_actor(t,k,a));st8(a+off,old);}assert(story_enemies::verify_actor(t,k,a));}
void create(uint32_t fn,uint32_t a,void(*init)(uint32_t)=nullptr){static void(*setup)(uint32_t);setup=init;probe=[a](Cpu*c,uint32_t){if(c->r[3]!=a)return false;if(setup)setup(a);c->r[3]=4;return true;};run(fn,a);probe=nullptr;}
// Fire Keese: bit 15 of the Keese word; mDamageType 0x80 (0 for the ordinary one); no Ice Keese in the game
void fire_keese_test(story_enemies::Token t){using namespace story_enemies;KeepClaims keep;constexpr uint32_t Fk=Actor+0x1C000;
 assert(parameters(Kind::Keese,"fire")==0xFFFF8000u&&parameters(Kind::Keese,"")==0xFFFF0000u&&!parameters(Kind::Keese,"ice"));
 actor(Fk,24,Kind::Keese);st32(Fk+0xB0,0xFFFF8000u);st8(Fk+0xAB9,0x80);assert(verify_actor(t,Kind::Keese,Fk));toggles(t,Kind::Keese,Fk,{0xAB9});
 bind(t,24);create(0x0219D568,Fk);assert(owns_actor(Fk));
 probe=[](Cpu*c,uint32_t f){if(f!=0x0219AAE8u)return false;st8(Fk+0x489,30);Cpu d=*c;d.r[3]=Fk;hooks.at(0x025D99E8)(&d);assert(d.r[3]==0xFFFFFFFFu);c->r[3]=1;return true;};run(0x0219AAE8,Fk);probe=nullptr;assert(is_defeated(t));}
// Seahat: Peahat type 1; the kill counter (event register 0x7EFF) written during its step is restored
void seahat_test(story_enemies::Token t){using namespace story_enemies;KeepClaims keep;constexpr uint32_t Sh=Actor+0x1A000,Info=Actor+0x1E000;
 assert(parameters(Kind::Peahat,"seahat")==0xFFFFFF01u&&parameters(Kind::Peahat,"")==0xFFFFFF00u);
 actor(Sh,23,Kind::Peahat);st32(Sh+0xB0,0xFFFFFF01u);st8(Sh+0x45A,1);assert(verify_actor(t,Kind::Peahat,Sh));toggles(t,Kind::Peahat,Sh,{0x45A,0xAB4+0x1B1});
 bind(t,23);create(0x023CF600,Sh);assert(owns_actor(Sh));
 st32(GD(0x101F84DC),Info);st8(Info+0x644+0x7E,5);
 probe=[](Cpu*c,uint32_t f){if(f!=0x023CC070u)return false;st8(Info+0x644+0x7E,6);st8(Sh+0x3A1,0);Cpu d=*c;d.r[3]=Sh;hooks.at(0x025D99E8)(&d);assert(d.r[3]==0xFFFFFFFFu);c->r[3]=1;return true;};run(0x023CC070,Sh);probe=nullptr;
 assert(ld8(Info+0x644+0x7E)==5&&is_defeated(t));st32(GD(0x101F84DC),0);}
// Boko Baba: fixed ordinary word; its grab request is refused without quarantine; it creates no actors while it runs
void boko_baba_test(story_enemies::Token t){using namespace story_enemies;KeepClaims keep;constexpr uint32_t Bo=B3;
 assert(parameters(Kind::BokoBaba)==0xFFFFFF00u);
 actor(Bo,120,Kind::BokoBaba);assert(verify_actor(t,Kind::BokoBaba,Bo));toggles(t,Kind::BokoBaba,Bo,{0x3DD,0x3DC,0x3E8,0xB18+0x1B1});
 st8(Bo+0x3DC,1);assert(!verify_actor(t,Kind::BokoBaba,Bo));st8(Bo+0x3E0,2);assert(verify_actor(t,Kind::BokoBaba,Bo));st8(Bo+0x3DC,0);st8(Bo+0x3E0,0);
 bind(t,120);create(0x020BFC50,Bo);assert(owns_actor(Bo));
 probe=[](Cpu*c,uint32_t f){if(f!=0x020BCEC8u)return false;st8(Bo+0x3E7,1);Cpu ev=*c;ev.r[3]=Bo;hooks.at(0x025D7B24)(&ev);assert(ev.r[3]==0&&ld8(Bo+0x3E7)==0);assert(deny_create(0xD6,2)&&deny_create(0xD5,1)&&deny_create(0x1CF,8));c->r[3]=1;return true;};run(0x020BCEC8,Bo);probe=nullptr;assert(available(t)&&!deny_create(0xD6,2));
 probe=[](Cpu*c,uint32_t f){if(f!=0x020BCEC8u)return false;st8(Bo+0x3E0,2);st8(Bo+0x3E1,21);st8(Bo+0x3DC,1);Cpu d=*c;d.r[3]=Bo;hooks.at(0x025D99E8)(&d);assert(d.r[3]==0xFFFFFFFFu);c->r[3]=1;return true;};run(0x020BCEC8,Bo);probe=nullptr;assert(is_defeated(t));}
// a Boko Baba holding Link (never possible once refused) is not let through
void boko_baba_hold_test(story_enemies::Token t){using namespace story_enemies;KeepClaims keep;constexpr uint32_t Bo=B3+0x2000;
 actor(Bo,125,Kind::BokoBaba);bind(t,125);create(0x020BFC50,Bo);assert(owns_actor(Bo));
 probe=[](Cpu*c,uint32_t f){if(f!=0x020BCEC8u)return false;st8(Bo+0x3E8,1);Cpu ev=*c;ev.r[3]=Bo;hooks.at(0x025D7B24)(&ev);assert(ev.r[3]==0);c->r[3]=1;return true;};run(0x020BCEC8,Bo);probe=nullptr;assert(!owns_actor(Bo));}
// Bubble: its declared colour on the ground, no switch or path, the grapple steal off; only its terminal steps count
void bubble_test(story_enemies::Token t){using namespace story_enemies;KeepClaims keep;constexpr uint32_t Bl=B3+0x4000;
 assert(parameters(Kind::Bubble,"blue")==0xFFFFFF01u&&parameters(Kind::Bubble,"red")==0xFFFFFF00u&&parameters(Kind::Bubble)==0xFFFFFF00u&&!parameters(Kind::Bubble,"skull"));
 std::memset(mem::ptr(Bl),0,0xE18);actor(Bl,121,Kind::Bubble);st32(Bl+0xB0,0xFFFFFF01u);st8(Bl+0x3EC,1);assert(verify_actor(t,Kind::Bubble,Bl));
 toggles(t,Kind::Bubble,Bl,{0x3EC,0x3ED,0x3F1,0x405,0x406,0x82C+0x1B1});st32(Bl+0x3F4,Actor);assert(!verify_actor(t,Kind::Bubble,Bl));st32(Bl+0x3F4,0);
 bind(t,121);create(0x020ADA5C,Bl,[](uint32_t a){st8(a+0x406,1);});assert(ld8(Bl+0x406)==0&&owns_actor(Bl));
 probe=[](Cpu*c,uint32_t f){if(f!=0x020AB020u)return false;st8(Bl+0x3EE,4);st16(Bl+0x422,42);st8(Bl+0x3A1,0);Cpu d=*c;d.r[3]=Bl;hooks.at(0x025D99E8)(&d);c->r[3]=1;return true;};run(0x020AB020,Bl);probe=nullptr;assert(is_defeated(t));}
// Octorok: the small one; a drop before its last death step quarantines; a retail Octorok (its rock too) runs untouched
void octorok_test(story_enemies::Token t){using namespace story_enemies;KeepClaims keep;constexpr uint32_t Oq=B3+0x6000;
 assert(parameters(Kind::Octorok)==0xFFFFFF00u);
 actor(Oq,122,Kind::Octorok);assert(verify_actor(t,Kind::Octorok,Oq));toggles(t,Kind::Octorok,Oq,{0x3DC,0x3DE,0xBA4+0x1B1});
 bind(t,122);create(0x023C3EC8,Oq);assert(owns_actor(Oq));
 probe=[](Cpu*c,uint32_t f){if(f!=0x023C1380u)return false;assert(!deny_create(0xE3,6));st8(Oq+0x3DF,0x14);st8(Oq+0x3E0,0x20);Cpu d=*c;d.r[3]=Oq;hooks.at(0x025D99E8)(&d);c->r[3]=1;return true;};run(0x023C1380,Oq);probe=nullptr;assert(!available(t)&&!is_defeated(t));
 std::memset(mem::ptr(Retail),0,0x1200);st16(Retail+0xE,0xE3);const unsigned c0=calls;run(0x023C1380,Retail);assert(calls==c0+1);}
int main(int argc,char**argv){using namespace story_enemies;const bool drowning_test=argc==2&&std::strcmp(argv[1],"--drowning")==0;const bool weapons=drowning_test||(argc==2&&std::strcmp(argv[1],"--weapons")==0);
 map_memory(Actor,0x20000);map_memory(B3,0x20000);map_memory(More,0x20000);map_memory(Arena,0x20000);map_memory(GD(0x101F0000),0x10000);map_memory(GD(0x10460000),0x10000);
#if STORY_ENEMIES_TEST_CODE_DELTA
 for(uint32_t canonical:{0x101F0000u,0x10460000u,0x10190000u,0x101B0000u}){map_memory(canonical,0x10000);std::memset(mem::ptr(canonical),0xA5,0x10000);}
#endif
 st32(GD(0x101F3EE0)+0xCE*4,Actor+0x8000);st32(Actor+0x8010,0xF10);st32(GD(0x101F3EE0)+0xD7*4,Actor+0x8100);st32(Actor+0x8110,0x10BC);st32(GD(0x101F3EE0)+0xF7*4,Actor+0x8600);st32(Actor+0x8610,0xEE4);st32(GD(0x101F3EE0)+0xD1*4,Actor+0x8700);st32(Actor+0x8710,0x12D8);st32(GD(0x101F3EE0)+0xC6*4,Actor+0x8800);st32(Actor+0x8810,0x11CC);st32(GD(0x101F3EE0)+0xC7*4,Actor+0x8A00);st32(Actor+0x8A10,0x574);st32(GD(0x101F3EE0)+0xD6*4,Actor+0x8D00);st32(Actor+0x8D10,0x1104);st32(GD(0x101F3EE0)+0xCF*4,Actor+0x8E00);st32(Actor+0x8E10,0xE18);st32(GD(0x101F3EE0)+0xE3*4,Actor+0x8F00);st32(Actor+0x8F10,0x1194);
 assert(story_enemy_ownership::configure(Arena,story_enemy_ownership::kStorageBytes,original,verify_actor));story_enemy_ownership::initialize_storage();
 std::string why;Spec cc{Kind::ChuChu,"m","cc","cc.dead",{}, {},1},ki{Kind::Keese,"m","ki","ki.dead",{}, {},1};
 const Token first=declare(cc,why),second=declare(ki,why);assert(first&&second&&first!=second);Spec unsupported=cc;unsupported.kind=Kind::Moblin;assert(!declare(unsupported,why));
 Token bk=0,mo=0;if(weapons){assert(configure_children({[](Token,Kind,uint32_t,uint32_t epoch){assert(epoch>=1);++child_scopes;return true;},[]{assert(child_scopes);--child_scopes;},[](Token,uint32_t){return children_ready;},[](Token,uint32_t){++child_retires;}},true));Spec b{Kind::Bokoblin,"m","bk","bk.dead",{}, {},1},m{Kind::Moblin,"m","mo","mo.dead",{}, {},1};bk=declare(b,why);mo=declare(m,why);assert(bk&&mo);st32(GD(0x101F3EE0)+0xBD*4,Actor+0x8200);st32(Actor+0x8210,0x19A4);st32(GD(0x101F3EE0)+0xBC*4,Actor+0x8300);st32(Actor+0x8310,0x343C);map_memory(GD(0x101B0000),0x10000);map_memory(GD(0x10190000),0x10000);}
 Spec pt{Kind::Miniblin,"m","pt","pt.dead",{}, {},1};const Token mini=declare(pt,why);assert(mini);Spec ph{Kind::Peahat,"m","ph","ph.dead",{}, {},1};const Token hat=declare(ph,why);assert(hat);Spec nz{Kind::Rat,"m","nz","nz.dead",{}, {},1};const Token rat=declare(nz,why);assert(rat);Spec nb{Kind::Rat,"m","nb","nb.dead",{}, {},1,0xFFFF0101u};const Token bomb_rat=declare(nb,why);assert(bomb_rat);Spec ng{Kind::RatHole,"m","ng","ng.dead",{}, {},1};const Token hole=declare(ng,why);assert(hole);
 Token tn=0;if(weapons){Spec d{Kind::Darknut,"m","tn","tn.dead",{}, {},1,parameters(Kind::Darknut,"mighty")};assert(d.params==0xFFFFFA54u&&!parameters(Kind::Darknut,"frozen"));tn=declare(d,why);assert(tn&&home_x(tn)==0xA0&&home_x(first)==0);st32(GD(0x101F3EE0)+0xBF*4,Actor+0x8900);st32(Actor+0x8910,0x1C5C);}
 Spec fk{Kind::Keese,"m","fk","fk.dead",{}, {},1,0xFFFF8000u};const Token fire_keese=declare(fk,why);assert(fire_keese);
 Spec sh{Kind::Peahat,"m","sh","sh.dead",{}, {},1,0xFFFFFF01u};const Token seahat=declare(sh,why);assert(seahat);
 Spec bo{Kind::BokoBaba,"m","bo","bo.dead",{}, {},1},bo2{Kind::BokoBaba,"m","bo2","bo2.dead",{}, {},1};const Token baba=declare(bo,why),baba2=declare(bo2,why);assert(baba&&baba2);
 Spec bl{Kind::Bubble,"m","bl","bl.dead",{}, {},1,0xFFFFFF01u};const Token bubble=declare(bl,why);assert(bubble);
 Spec oq{Kind::Octorok,"m","oq","oq.dead",{}, {},1};const Token octo=declare(oq,why);assert(octo);
 std::map<Kind,Token> table;for(const auto&n:story_enemies_more::kNatives){Spec t{n.kind,"m",n.name,std::string(n.name)+".dead",{}, {},1};table[n.kind]=declare(t,why);assert(table[n.kind]&&supported(n.kind)==n.offered);}
 {std::array<uint32_t,story_enemy_ownership::kKindCount> storage{Arena+0xB000,Arena+0xB030,Arena+0xB060,Arena+0xB090,Arena+0xB0C0,Arena+0xB0F0,Arena+0xB120,Arena+0xB150,Arena+0xB180,Arena+0xB1B0,Arena+0xB1E0,Arena+0xB210};uint32_t slot=0;for(const auto&n:story_enemies_more::kNatives){storage[unsigned(n.kind)]=Arena+0xB240+0x30*slot;st32(GD(0x101F3EE0)+n.descriptor.native_profile*4,Actor+0x9000+0x100*slot);st32(Actor+0x9010+0x100*slot,n.descriptor.bytes);++slot;}configure(original,invoke,storage);}assert(install([](uint32_t){return std::string();},[](uint32_t f,void(*fn)(Cpu*)){const uint32_t canonical=f-STORY_ENEMIES_TEST_CODE_DELTA;assert(known_address(canonical)&&!hooks.count(canonical));hooks[canonical]=fn;}));assert(hooks.size()==62+4*std::size(story_enemies_more::kNatives));
 fire_keese_test(fire_keese);
 seahat_test(seahat);
 boko_baba_test(baba);boko_baba_hold_test(baba2);
 bubble_test(bubble);
 octorok_test(octo);
 { // the table kinds: a placed copy with its fixed word verifies; each parameter-derived field, its ice
  // helper's death switch and a forged parameter word fail; only its terminal state's drop is its defeat
  uint32_t pid=40,at=More;
  auto make=[&](Kind k){const auto*n=story_enemies_more::find(k);const auto&d=n->descriptor;std::memset(mem::ptr(at),0,d.bytes);st32(at+4,++pid);st16(at+0xE,d.profile);st32(at+0xB4,n->vtable());st32(at+0xB0,n->params);st8(at+0x2DD,0xFF);st16(at+0x2D8,0xFFFF);
   for(size_t i=0;i<d.collider_count;++i){const auto&x=d.colliders[i];st32(at+x.object+0x44,at+x.stts);st32(at+x.stts+0xC,at);st32(at+x.stts+0x10,pid);if(x.helper)st32(at+x.helper,at);}};
  auto expect_field=[&](Token t,Kind k,uint32_t off){const uint8_t old=ld8(at+off);st8(at+off,uint8_t(old^0x21));assert(!verify_actor(t,k,at));st8(at+off,old);assert(verify_actor(t,k,at));};
  // Magtail: type 1, no switches, no path
  make(Kind::Magtail);st8(at+0x3D0,1);st8(at+0x3D3,0xFF);st8(at+0x3D4,0xFF);const Token mt=table[Kind::Magtail];assert(verify_actor(mt,Kind::Magtail,at));
  for(uint32_t off:{0x3D0u,0x3D1u,0x3D2u,0x3D3u,0x3D4u,0x3D5u,0x3D6u,0x3D7u,0x3D8u,0x1BCDu})expect_field(mt,Kind::Magtail,off);
  st32(at+0x3DC,0x10001000);assert(!verify_actor(mt,Kind::Magtail,at));st32(at+0x3DC,0);st32(at+0xB0,0xFFFF0000u);assert(!verify_actor(mt,Kind::Magtail,at));st32(at+0xB0,0xFFFF0001u);
  st8(at+0x570,4);assert(!verify_actor(mt,Kind::Magtail,at));st8(at+0x570,2);assert(verify_actor(mt,Kind::Magtail,at));
  // the curled Magtail is the game's own carry; carried while it crawls again it is not (the release follows)
  bind(mt,pid);run(story_enemies_more::find(Kind::Magtail)->create,at);assert(owns_actor(at)&&!deny_carry(at));st8(at+0x570,0);assert(deny_carry(at));st8(at+0x570,1);st32(at+0x2E0,0x2000);assert(!deny_carry(at));st32(at+0x2E0,0);assert(deny_carry(at));
  // a fall out of the world is no defeat; the drop of a damage mode >= 2 is
  {const unsigned c0=claims,d0=drops;st8(at+0x570,1);st8(at+0x1A14,0xFF);Cpu probe{};probe.r[3]=at;hooks.at(0x025D99E8)(&probe);assert(claims==c0&&drops==d0&&!available(mt));}
  assert(begin_epoch(mt,2));make(Kind::Magtail);st8(at+0x3D0,1);st8(at+0x3D3,0xFF);st8(at+0x3D4,0xFF);bind(mt,pid);run(story_enemies_more::find(Kind::Magtail)->create,at);
  {const unsigned c0=claims,d0=drops;st8(at+0x570,2);st8(at+0x1A14,2);Cpu probe{};probe.r[3]=at;hooks.at(0x025D99E8)(&probe);assert(claims==c0+1&&drops==d0&&probe.r[3]==0xFFFFFFFFu&&is_defeated(mt));}
  // Armos: radius 600 (0xFF), active, no switch
  at+=0x2000;make(Kind::Armos);st8(at+0x3E5,0xFF);st8(at+0x3E7,0xFF);const Token am2=table[Kind::Armos];assert(verify_actor(am2,Kind::Armos,at));
  for(uint32_t off:{0x3E4u,0x3E5u,0x3E6u,0x3E7u,0x3ECu,0xD59u})expect_field(am2,Kind::Armos,off);
  bind(am2,pid);run(story_enemies_more::find(Kind::Armos)->create,at);assert(owns_actor(at)&&deny_carry(at));
  {const unsigned c0=claims;st8(at+0x3E8,2);st8(at+0x3E9,25);st8(at+0x3A1,1);assert(!story_enemies_more::armos_terminal(at));st8(at+0x3A1,0);Cpu probe{};probe.r[3]=at;hooks.at(0x025D99E8)(&probe);assert(claims==c0+1&&is_defeated(am2));}
  claims=drops=0;
 }
 { // Rat: a rat of a rat hole (arg0 1), its own type (ordinary or bomb rat), unfrozen
  constexpr uint32_t Rat=Actor+0x12000;actor(Rat,16,Kind::Rat);assert(parameters(Kind::Rat)==0xFFFF0001u);
  assert(verify_actor(rat,Kind::Rat,Rat));st8(Rat+0x3D1,1);assert(!verify_actor(rat,Kind::Rat,Rat));st32(Rat+0xB0,0xFFFF0101u);assert(verify_actor(bomb_rat,Kind::Rat,Rat));st8(Rat+0x3D1,0);assert(!verify_actor(bomb_rat,Kind::Rat,Rat));st32(Rat+0xB0,0xFFFF0001u);assert(verify_actor(rat,Kind::Rat,Rat));
  st8(Rat+0x3D0,0);assert(!verify_actor(rat,Kind::Rat,Rat));st8(Rat+0x3D0,1);st8(Rat+0xBE0+0x1B1,1);assert(!verify_actor(rat,Kind::Rat,Rat));st8(Rat+0xBE0+0x1B1,0);
 }
 { // Rat hole: no range, one rat, no shop keeper, no path; settled (closed) without drops, once
  constexpr uint32_t Hole=Actor+0x14000;actor(Hole,17,Kind::RatHole);assert(parameters(Kind::RatHole)==0xFF010100u&&ld32(Hole+0xB0)==0xFF010100u);
  assert(verify_actor(hole,Kind::RatHole,Hole));
  for(uint32_t off:{0x3D4u,0x3D5u,0x3D6u,0x405u}){const uint8_t old=ld8(Hole+off);st8(Hole+off,uint8_t(old^1));assert(!verify_actor(hole,Kind::RatHole,Hole));st8(Hole+off,old);}
  st32(Hole+0x3F4,1);assert(!verify_actor(hole,Kind::RatHole,Hole));st32(Hole+0x3F4,0);assert(verify_actor(hole,Kind::RatHole,Hole));
  { // a refused create (a private-profile actor nothing owns) fails like a retail create: constructed first,
    // so fopAc_Delete can take it; its own delete is skipped (nothing of it was built)
   constexpr uint32_t Stray=Actor+0x16000;std::memset(mem::ptr(Stray),0,0x574);st32(Stray+4,77);st16(Stray+0xE,0x7FF1);
   const unsigned before=calls;Cpu c{};c.r[3]=Stray;hooks.at(0x0231175C)(&c);assert(c.r[3]==5&&base_constructs==1&&calls==before&&native_actor_refusal::base_only(Stray));
   c={};c.r[3]=Stray;hooks.at(0x023115BC)(&c);assert(c.r[3]==1&&calls==before);}
  const auto claims_before=claims;assert(!is_defeated(hole)&&settle(hole)&&is_defeated(hole)&&!settle(hole)&&!available(hole));claims=claims_before;  // the hole's marker only: later checks count enemy claims
 }
 { // Peahat: the Peahat type only (never the Seahat with its kill-counter register), unfrozen
  constexpr uint32_t Hat=Actor+0x10000;actor(Hat,15,Kind::Peahat);assert(parameters(Kind::Peahat)==0xFFFFFF00u&&ld32(Hat+0xB0)==0xFFFFFF00u);
  assert(verify_actor(hat,Kind::Peahat,Hat));st8(Hat+0x45A,1);assert(!verify_actor(hat,Kind::Peahat,Hat));st8(Hat+0x45A,0);
  st8(Hat+0xAB4+0x1B1,1);assert(!verify_actor(hat,Kind::Peahat,Hat));st8(Hat+0xAB4+0x1B1,0);assert(verify_actor(hat,Kind::Peahat,Hat));
 }
 { // Miniblin: fixed ordinary parameters (never respawns, no switches) and an unfrozen ice helper
  constexpr uint32_t Mini=Actor+0xE000;actor(Mini,14,Kind::Miniblin);assert(parameters(Kind::Miniblin)==0xFFFF0A01u&&ld32(Mini+0xB0)==0xFFFF0A01u);
  assert(verify_actor(mini,Kind::Miniblin,Mini));
  for(uint32_t off:{0x3D0u,0x3D1u,0x3D3u,0x3D4u,0x3D6u}){const uint8_t old=ld8(Mini+off);st8(Mini+off,uint8_t(old^1));assert(!verify_actor(mini,Kind::Miniblin,Mini));st8(Mini+off,old);}
  st8(Mini+0x8FC+0x1B1,1);assert(!verify_actor(mini,Kind::Miniblin,Mini));st8(Mini+0x8FC+0x1B1,0);
  st32(Mini+0xB0,0xFFFF0A00u);assert(!verify_actor(mini,Kind::Miniblin,Mini));st32(Mini+0xB0,0xFFFF0A01u);assert(verify_actor(mini,Kind::Miniblin,Mini));
 }
 if(weapons){actor(Bokoblin,12,Kind::Bokoblin);actor(Moblin,13,Kind::Moblin);bind(bk,12);bind(mo,13);throw_create=true;try{run(0x0209F918,Bokoblin);assert(false);}catch(const std::runtime_error&){}throw_create=false;assert(ld8(GD(0x10191468))==0&&child_scopes==0);run(0x0209F918,Bokoblin);run(0x021CCB00,Moblin);assert(ld8(GD(0x10191468))==0&&ld8(GD(0x101BA690))==0&&child_scopes==0);const unsigned prior=calls;run(0x0209BD1C,Bokoblin);assert(calls==prior);children_ready=true;run(0x0209BD1C,Bokoblin);run(0x021C9B1C,Moblin);assert(child_scopes==0);const unsigned before_callback=calls;run(0x021C9AA0,Moblin);assert(calls==before_callback);// Exact two-byte native BK learning scratch restores through nested and exceptional calls.
 initialize_weapon_scratch();const auto saved_scratch=weapon_snapshot();nested_weapon=true;run(0x0209BD1C,Bokoblin);nested_weapon=false;assert(ld16(GD(0x10462474))==0x7777&&child_scopes==0&&weapon_snapshot()==saved_scratch);throw_execute=true;try{run(0x0209BD1C,Bokoblin);assert(false);}catch(const std::runtime_error&){}throw_execute=false;assert(ld16(GD(0x10462474))==0x7777&&child_scopes==0&&weapon_snapshot()==saved_scratch);throw_execute=true;try{run(0x021C9B1C,Moblin);assert(false);}catch(const std::runtime_error&){}throw_execute=false;assert(weapon_snapshot()==saved_scratch&&child_scopes==0);run(0x0209971C,Bokoblin);run(0x021C7344,Moblin);assert(weapon_snapshot()==saved_scratch);
 // Fixed raw params cannot authorize stale derived retail trigger/path metadata.
 st8(Bokoblin+0x3D6,1);assert(!verify_actor(bk,Kind::Bokoblin,Bokoblin));st8(Bokoblin+0x3D6,0);st8(Moblin+0x2D72,1);assert(!verify_actor(mo,Kind::Moblin,Moblin));st8(Moblin+0x2D72,0);st32(Moblin+0x2D74,Actor);assert(!verify_actor(mo,Kind::Moblin,Moblin));st32(Moblin+0x2D74,0);st8(Moblin+0x2D70,1);assert(!verify_actor(mo,Kind::Moblin,Moblin));st8(Moblin+0x2D70,0);
 // Only the selected child of the current generation may normalize native references.
 story_enemy_ownership::LineageInfo gone{};gone.role=story_enemy_ownership::Lineage::EnemyChild;gone.source=bk;gone.source_actor=Bokoblin;gone.source_pid=12;gone.epoch=1;gone.source_valid=true;gone.pid=99;
 st32(Bokoblin+0x1360,99);st32(Bokoblin+0xC90,1);st32(Bokoblin+0xC94,2);st8(Bokoblin+0x1374,1);std::array<uint8_t,0x19A4> unchanged{};std::memcpy(unchanged.data(),mem::ptr(Bokoblin),unchanged.size());gone.pid=100;child_retired(gone);assert(std::memcmp(unchanged.data(),mem::ptr(Bokoblin),unchanged.size())==0);gone.pid=99;gone.epoch=2;child_retired(gone);assert(std::memcmp(unchanged.data(),mem::ptr(Bokoblin),unchanged.size())==0);gone.epoch=1;child_retired(gone);assert(ld32(Bokoblin+0x1360)==0xFFFFFFFFu&&ld32(Bokoblin+0xC90)==0&&ld32(Bokoblin+0xC94)==0&&ld8(Bokoblin+0x1374)==0&&ld8(Bokoblin+0x1353)==0&&ld16(Bokoblin+0x49C+6)==0&&available(bk)&&claims==0);run(0x0209BD1C,Bokoblin);
 gone.source=mo;gone.source_actor=Moblin;gone.source_pid=13;gone.pid=98;st32(Moblin+0x2D60,98);st8(Moblin+0x10B0,1);st8(Moblin+0x10B1,2);st8(Moblin+0x10B3,1);child_retired(gone);assert(ld32(Moblin+0x2D60)==0xFFFFFFFFu&&ld8(Moblin+0x10B0)==1&&ld8(Moblin+0x10B1)==2&&ld8(Moblin+0x10B3)==0&&available(mo)&&claims==0);
 gone.pid=97;st32(Moblin+0x2D60,97);st8(Moblin+0x10B0,0);st8(Moblin+0x10B1,1);st8(Moblin+0x10B3,1);child_retired(gone);assert(ld32(Moblin+0x2D60)==0xFFFFFFFFu&&ld8(Moblin+0x10B0)==0&&ld8(Moblin+0x10B1)==1&&ld8(Moblin+0x10B3)==0&&available(mo)&&claims==0);run(0x021C9B1C,Moblin);
 if(drowning_test){unsigned epoch=1,pid=100;auto fresh=[&](){assert(begin_epoch(bk,++epoch));actor(Bokoblin,++pid,Kind::Bokoblin);bind(bk,pid);defeated[3]=false;claims=0;run(0x0209F918,Bokoblin);};
 drown_mode=true;drown_lr=0x02044FD0;run(0x0209BD1C,Bokoblin);assert(claims==0);fresh();drown_lr=0x02044FCC;foreign_helper=true;run(0x0209BD1C,Bokoblin);assert(claims==0);foreign_helper=false;fresh();bad_helper=true;run(0x0209BD1C,Bokoblin);assert(claims==0&&!available(bk));bad_helper=false;fresh();bad_type=true;run(0x0209BD1C,Bokoblin);assert(claims==0&&!available(bk));bad_type=false;fresh();request_ok=false;run(0x0209BD1C,Bokoblin);assert(claims==0);request_ok=true;fresh();request_queued=false;run(0x0209BD1C,Bokoblin);assert(claims==0);request_queued=true;fresh();run(0x0209BD1C,Bokoblin);assert(claims==1&&!available(bk));run(0x0209BD1C,Bokoblin);assert(claims==1);fresh();fail_claim=true;run(0x0209BD1C,Bokoblin);assert(claims==1&&!available(bk)&&!defeated[3]);fail_claim=false;fresh();drown_mode=false;run(0x0209F164,Bokoblin);assert(claims==0);fresh();claims=0;
 }
 terminal=true;run(0x0209BD1C,Bokoblin);run(0x021C9B1C,Moblin);assert(claims==2&&drops==0);terminal=false;run(0x0209F164,Bokoblin);run(0x021CC44C,Moblin);assert(child_retires==(drowning_test?3u:2u));st16(Retail+0xE,0xBD);run(0x0209F918,Retail);assert(ld8(GD(0x10191468))==0);claims=creates=deletes=0;}
 if(weapons){ // Darknut (the Mighty variant: helmet, shield, cape)
  namespace own=story_enemy_ownership;map_memory(GD(0x101D0000),0x10000);
  const uint32_t Dk=0x20040000;map_memory(Dk,0x10000);st32(Dk+0xB0,0xFFFFFA54u);actor(Dk,20,Kind::Darknut);st32(Dk+0xB0,0xFFFFFA54u);st8(Dk+0x3D9,0xFA);st8(Dk+0x4DC,5);st8(Dk+0x4DD,5);st16(Dk+0x2F8,0xA0);assert(verify_actor(tn,Kind::Darknut,Dk));
  // every fixed byte is checked: behaviour, sight, path, switches, colour, equipment and its x word, the path
  // pointer and index, the HIO latch, the boss camera, the ice death switch, an event-only action, a boss status
  for(uint32_t off:{0x3D8u,0x3D9u,0x3DAu,0x3DBu,0x3DCu,0x3DDu,0x4DCu,0x4DDu,0x1574u,0x1582u,0x1648u,0x1674u+0x1B1u}){const uint8_t old=ld8(Dk+off);st8(Dk+off,uint8_t(old^1));assert(!verify_actor(tn,Kind::Darknut,Dk));st8(Dk+off,old);}
  st16(Dk+0x2F8,0x60);assert(!verify_actor(tn,Kind::Darknut,Dk));st16(Dk+0x2F8,0xA0);st16(Dk+0x2FC,0);assert(!verify_actor(tn,Kind::Darknut,Dk));st16(Dk+0x2FC,0xFF);
  st32(Dk+0x1578,Dk);assert(!verify_actor(tn,Kind::Darknut,Dk));st32(Dk+0x1578,0);st16(Dk+0x596,25);assert(!verify_actor(tn,Kind::Darknut,Dk));st16(Dk+0x596,23);assert(!verify_actor(tn,Kind::Darknut,Dk));st16(Dk+0x596,20);assert(verify_actor(tn,Kind::Darknut,Dk));st16(Dk+0x596,22);assert(verify_actor(tn,Kind::Darknut,Dk));st16(Dk+0x596,0);
  st32(Dk+0x2E0,0x04000000u);assert(!verify_actor(tn,Kind::Darknut,Dk));st32(Dk+0x2E0,0);assert(verify_actor(tn,Kind::Darknut,Dk));
  // create: the HIO latch is held (asserted by the native stand-in), the shared statics come back, and the
  // three nested executes of daTn_Create run natively inside the create's scope
  bind(tn,20);st8(GD(0x1046E73C),0);st8(GD(0x1046E73D),7);st8(GD(0x101D2468),0);children_ready=false;const unsigned scopes_before=child_scopes;run(0x024C0B0C,Dk);
  assert(tn_nested==3&&tn_execs==3&&ld8(GD(0x1046E73C))==0&&ld8(GD(0x1046E73D))==7&&ld8(GD(0x101D2468))==0&&child_scopes==scopes_before&&own::lookup_actor(Dk)==tn);
  // execute waits for its sword and cape (children not ready), then runs and keeps the shared search scratch
  run(0x024BD170,Dk);assert(tn_execs==3);children_ready=true;st32(GD(0x1046E718),1);st32(GD(0x1046E740),0x77);run(0x024BD170,Dk);assert(tn_execs==4&&ld32(GD(0x1046E718))==1&&ld32(GD(0x1046E740))==0x77&&child_scopes==scopes_before);
  // the native loss of its sword or cape clears exactly that reference
  own::LineageInfo gone{};gone.role=own::Lineage::EnemyChild;gone.source=tn;gone.source_actor=Dk;gone.source_pid=20;gone.epoch=1;gone.source_valid=true;gone.pid=300;
  st32(Dk+0x1560,300);st32(Dk+0xD84,1);st32(Dk+0xD88,2);st8(Dk+0x155C,1);st32(Dk+0x1598,301);child_retired(gone);assert(ld32(Dk+0x1560)==0xFFFFFFFFu&&ld32(Dk+0xD84)==0&&ld32(Dk+0xD88)==0&&ld8(Dk+0x155C)==0&&ld32(Dk+0x1598)==301);
  gone.pid=301;child_retired(gone);assert(ld32(Dk+0x1598)==0xFFFFu&&available(tn)&&claims==0);
  // a live sword and cape of this Darknut are removed with it
  const uint32_t Sw=0x20048000,Cp=0x2004A000;std::memset(mem::ptr(Sw),0,0x1000);st32(Sw+4,310);st16(Sw+0xE,0x1CF);st32(Sw+0xB0,3);std::memset(mem::ptr(Cp),0,0x4288);st32(Cp+4,311);st16(Cp+0xE,0xC0);
  for(auto kid:{std::make_pair(Sw,310u),std::make_pair(Cp,311u)}){const uint32_t t=own::reserve_child_pending(tn,Dk,20,ld16(kid.first+0xE),ld32(kid.first+0xB0),1);assert(t&&own::commit_child_pending(t,kid.second)&&own::admit_lineage(kid.second,kid.first));}
  // defeat: the disappear is claimed (no native drop), then the delete takes the sword and cape along
  terminal=true;run(0x024BD170,Dk);terminal=false;assert(claims==1&&drops==0&&!available(tn));
  kid_deletes=0;const unsigned deleted_before=deletes;run(0x024C0194,Dk);assert(deletes==deleted_before+1&&kid_deletes==2&&claims==1&&!own::lookup_actor(Dk));
  claims=creates=deletes=0;}
 actor(Actor,10,Kind::ChuChu);actor(Second,11,Kind::Keese);bind(first,10);bind(second,11);run(0x02110D4C,Actor);run(0x0219D568,Second);assert(creates==2&&claims==0&&owns_actor(Actor)&&deny_carry(Actor));
 st32(GD(0x10462CF0),7);st32(GD(0x10464B28),9);run(0x0210DD3C,Actor);assert(ld32(GD(0x10462CF0))==7&&ld32(GD(0x10464B28))==9&&claims==0);
 throw_execute=true;try{run(0x0210DD3C,Actor);assert(false);}catch(const std::runtime_error&){}throw_execute=false;assert(ld32(GD(0x10462CF0))==7&&ld32(GD(0x10464B28))==9);
 run(0x0219AAE8,Second);assert(ld32(GD(0x10464B28))==9);
 // Drawing and ordinary deletion cannot award defeat rewards.
 run(0x0210BFEC,Actor);run(0x02199748,Second);assert(claims==0);run(0x0219CE98,Second);assert(claims==0&&deletes==1);
 terminal=true;run(0x0210DD3C,Actor);assert(claims==1&&drops==0&&!available(first));run(0x0210DD3C,Actor);assert(claims==1);terminal=false;
 // Restored snapshot reopens its own transactional marker and identity.
 defeated[1]=false;story_enemy_ownership::state_loaded();state_loaded();bind(first,10,true);freeze_terminal=true;run(0x0210DD3C,Actor);assert(claims==2&&drops==0&&!(ld32(Actor+0x39C)&0x10));freeze_terminal=false;
 // Retail paths are delegated byte-for-byte and still create their own drops.
 std::memset(mem::ptr(Retail),0,0x1800);st16(Retail+0xE,0xCE);run(0x025D99E8,Retail);assert(drops==1);
 // A forged reserved identity is denied rather than falling through to retail.
 st8(Actor+0x3F9,7);assert(!verify_actor(first,Kind::ChuChu,Actor));const unsigned before=calls;run(0x0210DD3C,Actor);assert(calls==before);
 // An obsolete generation still releases its exact native class resources.
 assert(begin_epoch(first,2));const unsigned deleted_before=deletes,claimed_before=claims;run(0x021104B4,Actor);assert(deletes==deleted_before+1&&claims==claimed_before);st32(Actor+0xB4,0x12345678);run(0x021104B4,Actor);assert(deletes==deleted_before+1&&claims==claimed_before);
 #if STORY_ENEMIES_TEST_CODE_DELTA
 assert(ld32(0x101F3EE0)==0xA5A5A5A5u&&ld16(0x10462474)==0xA5A5&&ld8(0x10191468)==0xA5);
#endif
 return 0;
}

// SPDX-License-Identifier: MPL-2.0
#include "native_actor_refusal.h"
#include "story_enemies.h"
#include "story_enemy_variants.h"
#include "native_actor_profiles.h"
#include "guest_addr.h"
#include "runtime.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
namespace story_enemies {namespace {
struct Entry {Spec spec;Token token=0;story_items::EnemyMarkerHandle marker{};uint32_t pid=0xFFFFFFFFu,address=0;bool quarantine=false,claiming=false,search_known=false,creating=false;uint8_t search_mode=0;};
std::vector<Entry> entries;std::array<uint32_t,story_enemy_ownership::kKindCount> profiles{};
void(*original)(Cpu*,uint32_t)=nullptr;uint32_t(*invoke)(Cpu*,uint32_t,std::initializer_list<uint32_t>)=nullptr;
std::function<void(Token,uint32_t)> retired;bool installed=false,editor_allowed=false;ChildBridge children;bool weapon_admission=false;
thread_local Entry* executing=nullptr;thread_local Entry* ice_owner=nullptr;
struct DrowningScope {Entry*entry=nullptr;uint32_t helper=0;bool queued=false;};thread_local DrowningScope* drowning=nullptr;
const uint32_t CCcreate=GC(0x02110D4C),CCexec=GC(0x0210DD3C),CCdraw=GC(0x0210BFEC),CCdelete=GC(0x021104B4);
const uint32_t KIcreate=GC(0x0219D568),KIexec=GC(0x0219AAE8),KIdraw=GC(0x02199748),KIdelete=GC(0x0219CE98);
const uint32_t BKcreate=GC(0x0209F918),BKexec=GC(0x0209BD1C),BKdraw=GC(0x0209971C),BKdelete=GC(0x0209F164);
const uint32_t MOcreate=GC(0x021CCB00),MOexec=GC(0x021C9B1C),MOdraw=GC(0x021C7344),MOdelete=GC(0x021CC44C);
// d_a_pt (Miniblin): daPt_Create, daPt_Execute, daPt_Draw, daPt_Delete
const uint32_t PTcreate=GC(0x02446E54),PTexec=GC(0x02446374),PTdraw=GC(0x02445EE4),PTdelete=GC(0x024468E4);
// d_a_ph (Peahat): daPH_Create, daPH_Execute, daPH_Draw, daPH_Delete
const uint32_t PHcreate=GC(0x023CF600),PHexec=GC(0x023CC070),PHdraw=GC(0x023C9994),PHdelete=GC(0x023CEF04);
// d_a_nz (Rat): daNZ_Create, daNZ_Execute, daNZ_Draw, daNZ_Delete
const uint32_t NZcreate=GC(0x0230F32C),NZexec=GC(0x0230BEF4),NZdraw=GC(0x0230AE30),NZdelete=GC(0x0230EDD0);
// d_a_tn (Darknut): daTn_Create, daTn_Execute, daTn_Draw, daTn_Delete
const uint32_t TNcreate=GC(0x024C0B0C),TNexec=GC(0x024BD170),TNdraw=GC(0x024BA238),TNdelete=GC(0x024C0194);
// d_a_tn statics: search_sp, s_check (the native boss-Darknut event counter), hio_set, and the weapon/bomb
// search scratch (target_info_count and its ten entries)
const uint32_t TNsearchSp=GD(0x1046E73C),TNsCheck=GD(0x1046E73D),TNhioSet=GD(0x101D2468),TNtargetCount=GD(0x1046E718),TNtargets=GD(0x1046E740);
// d_a_nzg (rat hole): daNZG_Create, daNZG_Execute, daNZG_Draw, daNZG_Delete
const uint32_t NGcreate=GC(0x0231175C),NGexec=GC(0x02311240),NGdraw=GC(0x023111D8),NGdelete=GC(0x023115BC);
// d_a_bo (Boko Baba): daBO_Create, daBO_Execute, daBO_Draw, daBO_Delete
const uint32_t BOcreate=GC(0x020BFC50),BOexec=GC(0x020BCEC8),BOdraw=GC(0x020BBAEC),BOdelete=GC(0x020BF574);
constexpr Kind kKinds[]={Kind::ChuChu,Kind::Bokoblin,Kind::Moblin,Kind::Keese,Kind::Miniblin,Kind::Peahat,Kind::Rat,Kind::Darknut,Kind::RatHole,Kind::BokoBaba,Kind::Bubble};
// d_a_bl (Bubble): daBL_Create, daBL_Execute, daBL_Draw, daBL_Delete
const uint32_t BLcreate=GC(0x020ADA5C),BLexec=GC(0x020AB020),BLdraw=GC(0x020A8BDC),BLdelete=GC(0x020AD59C);
const uint32_t Disappear=GC(0x025D99E8),Ice=GC(0x020402C8),TableItem=GC(0x025D9154),KiSearch=GC(0x021992DC),RoomSearch=GC(0x025D9898);
Entry* get(Token t){for(auto& e:entries)if(e.token==t)return &e;return nullptr;}
uint32_t params_of(const Spec& s){return s.params?s.params:parameters(s.kind);}
std::function<uint32_t(Token)> route_provider;
// a patrolling Bokoblin's path pointer (bk ppd, +0x1378) is its route record; every other Bokoblin has none
uint32_t patrol_route(const Entry&e){return (e.spec.kind==Kind::Bokoblin||e.spec.kind==Kind::Moblin)&&story_enemy_variants::patrol_word(params_of(e.spec))&&route_provider?route_provider(e.token):0u;}
bool memory(uint32_t a,uint32_t bytes){return !(a&3)&&a>=0x10000000u&&bytes&&a<=0x50000000u-bytes;}
bool reserved(uint32_t a){return memory(a,0x10)&&((ld16(a+0xE)>=0x7FF7&&ld16(a+0xE)<=0x7FFA)||ld16(a+0xE)==0x7FF4||(ld16(a+0xE)>=0x7FF0&&ld16(a+0xE)<=0x7FF3)||ld16(a+0xE)==0x7FEA||ld16(a+0xE)==0x7FE9);}
uint32_t native_vtable(Kind k){return GD(k==Kind::ChuChu?0x1000C5A0:k==Kind::Bokoblin?0x1000957C:k==Kind::Moblin?0x10014E9C:k==Kind::Miniblin?0x10038234:k==Kind::Peahat?0x10034494:k==Kind::Rat?0x1002442C:k==Kind::Darknut?0x10040490:k==Kind::RatHole?0x10024598:k==Kind::BokoBaba?0x1000A184:k==Kind::Bubble?0x10009964:0x10012C24);}
bool cleanup_identity(uint32_t a,uint32_t fn){for(Kind k:kKinds){const auto&d=story_enemy_ownership::descriptor(k);const uint32_t destructor=k==Kind::ChuChu?CCdelete:k==Kind::Bokoblin?BKdelete:k==Kind::Moblin?MOdelete:k==Kind::Miniblin?PTdelete:k==Kind::Peahat?PHdelete:k==Kind::Rat?NZdelete:k==Kind::Darknut?TNdelete:k==Kind::RatHole?NGdelete:k==Kind::BokoBaba?BOdelete:k==Kind::Bubble?BLdelete:KIdelete;if(fn==destructor&&memory(a,d.bytes)&&ld16(a+0xE)==d.profile&&ld32(a+0xB4)==native_vtable(k)&&(story_enemy_variants::has_variants(kind_name(k))?story_enemy_variants::declared_params(kind_name(k),ld32(a+0xB0)):(ld32(a+0xB0)==parameters(k)||(k==Kind::Moblin&&ld32(a+0xB0)==story_enemy_variants::moblin_patrol(parameters(k)))))&&ld8(a+0x2DD)==0xFF&&ld16(a+0x2D8)==0xFFFF){if(k==Kind::Bokoblin)st8(a+0x137D,0);if(k==Kind::Moblin)st8(a+0x2E0E,0);return true;}}return false;}
uint32_t ice_offset(Kind k){return k==Kind::ChuChu?0x924:k==Kind::Bokoblin?0x13BC:k==Kind::Moblin?0x2E54:k==Kind::Miniblin?0x8FC:k==Kind::Peahat?0xAB4:k==Kind::Rat?0xBE0:k==Kind::Darknut?0x1674:k==Kind::BokoBaba?0xB18:k==Kind::Bubble?0x82C:0xAD8;}
Entry* find(uint32_t a,bool initialized=true){if(!memory(a,0x10))return nullptr;for(auto&e:entries){const auto&d=story_enemy_ownership::descriptor(e.spec.kind);if(!memory(a,d.bytes)||e.pid==0xFFFFFFFFu||ld32(a+4)!=e.pid||ld16(a+0xE)!=d.profile||ld32(a+0xB0)!=params_of(e.spec)||ld8(a+0x2DD)!=0xFF)continue;if(!initialized||verify_actor(e.token,e.spec.kind,a))return &e;}return nullptr;}
void disable(Entry&e,uint32_t a){e.quarantine=true;if(a)story_enemy_ownership::retire_before_delete(a);}
void trace(const Entry&e,uint32_t a,const char*what,uint32_t value=0){LOG("[native_enemy] %s:%s event=%s actor=%08X pid=%08X profile=%04X kind=%u value=%u",e.spec.mod.c_str(),e.spec.id.c_str(),what,a,e.pid,unsigned(ld16(a+0xE)),unsigned(e.spec.kind),value);}
void no_carry(uint32_t a){st32(a+0x39C,ld32(a+0x39C)&~0x10u);st8(a+0x3A8,0xFF);st8(a+0x3A9,0);}
// A black ChuChu turned to stone (CC_ACTION_STONE 6) is the one owned enemy Link may lift and throw: the
// native carry, its throw and the break on landing only move this actor (no save bits, no switches).
bool black_chuchu(const Spec&s){return s.kind==Kind::ChuChu&&((params_of(s)>>8)&0xFF)==3;}
bool liftable_stone(const Spec&s,uint32_t a){return black_chuchu(s)&&ld8(a+0x3FC)==6;}
void keep_owned(const Spec&s,uint32_t a){if(liftable_stone(s,a)){st8(a+0x3A8,0xFF);st8(a+0x3A9,0);}else no_carry(a);}
bool claim(Entry&e,uint32_t a){if(e.claiming)return false;e.claiming=true;struct ClaimGuard{bool&flag;~ClaimGuard(){flag=false;}}claim_guard{e.claiming};const auto result=story_items::claim_enemy_drops(e.marker,e.spec.mod,e.spec.drops);e.claiming=false;trace(e,a,"defeat-claim",unsigned(result));if(result==story_items::EnemyClaimStatus::Committed||result==story_items::EnemyClaimStatus::AlreadyDefeated)return true;disable(e,a);return false;}
// These globals are native temporary search results, not progress. Preserve them
// through exceptions and nested native execution; owned cohorts cannot leak out.
// The Seahat counts kills in event register 0x7EFF (the save's event bytes
// at [0x101F84DC]+0x644, byte 0x7E; the sea ambushes read it). An owned enemy never changes it: the byte is
// restored after each of its steps.
uint32_t kill_counter(){const uint32_t info=ld32(GD(0x101F84DC));return memory(info,0x700)?info+0x644+0x7E:0;}
struct Scope {Entry* prior;Entry&entry;std::array<uint32_t,13> saved{};std::array<uint32_t,28> weapon_saved{};std::array<uint32_t,11> tn_saved{};uint32_t counter=kill_counter();uint8_t counter_value=counter?ld8(counter):0;Scope(Entry&e):prior(executing),entry(e){executing=&e;if(e.spec.kind==Kind::Darknut){tn_saved[0]=ld32(TNtargetCount);for(unsigned i=0;i<10;++i)tn_saved[i+1]=ld32(TNtargets+4*i);}saved[0]=ld32(GD(0x10462CF0));for(unsigned i=0;i<10;++i)saved[i+1]=ld32(GD(0x10462D00)+4*i);saved[11]=ld32(GD(0x10464B28));saved[12]=ld32(GD(0x10464B2C));if(e.spec.kind==Kind::Bokoblin||e.spec.kind==Kind::Moblin){const uint32_t base=GD(e.spec.kind==Kind::Bokoblin?0x10462320:0x10465498);weapon_saved[0]=ld32(base);for(unsigned i=0;i<10;++i)weapon_saved[i+1]=ld32(base+0x10+4*i);weapon_saved[11]=ld32(GD(0x1046231C));weapon_saved[12]=ld32(GD(0x1046232C));weapon_saved[13]=ld8(GD(0x104654A4));weapon_saved[14]=ld8(GD(0x101BA692));weapon_saved[20]=ld16(GD(0x10462474));const uint32_t offsets[]={0x28,0x34,0x3C,0x44,0x48};for(unsigned i=0;i<5;++i)weapon_saved[15+i]=ld32(GD(0x104654E0)+offsets[i]);if(e.search_known)st8(GD(e.spec.kind==Kind::Bokoblin?0x1046232D:0x104654A4),e.search_mode);}}~Scope(){if(counter&&ld8(counter)!=counter_value){LOG("[native_enemy] %s:%s kill counter 0x7EFF kept at %u (the game wrote %u)",entry.spec.mod.c_str(),entry.spec.id.c_str(),unsigned(counter_value),unsigned(ld8(counter)));st8(counter,counter_value);}if(entry.spec.kind==Kind::Darknut){st32(TNtargetCount,tn_saved[0]);for(unsigned i=0;i<10;++i)st32(TNtargets+4*i,tn_saved[i+1]);}st32(GD(0x10462CF0),saved[0]);for(unsigned i=0;i<10;++i)st32(GD(0x10462D00)+4*i,saved[i+1]);st32(GD(0x10464B28),saved[11]);st32(GD(0x10464B2C),saved[12]);if(entry.spec.kind==Kind::Bokoblin||entry.spec.kind==Kind::Moblin){const uint32_t base=GD(entry.spec.kind==Kind::Bokoblin?0x10462320:0x10465498);st32(base,weapon_saved[0]);for(unsigned i=0;i<10;++i)st32(base+0x10+4*i,weapon_saved[i+1]);st32(GD(0x1046231C),weapon_saved[11]);st32(GD(0x1046232C),weapon_saved[12]);st8(GD(0x104654A4),uint8_t(weapon_saved[13]));st8(GD(0x101BA692),uint8_t(weapon_saved[14]));st16(GD(0x10462474),uint16_t(weapon_saved[20]));const uint32_t offsets[]={0x28,0x34,0x3C,0x44,0x48};for(unsigned i=0;i<5;++i)st32(GD(0x104654E0)+offsets[i],weapon_saved[15+i]);}executing=prior;}};
// test aid: WWHD_ENEMY_TRACE=1 logs each owned enemy's execute path every 30 steps (place, action, what ran)
bool enemy_trace(){static const bool on=[]{const char*v=std::getenv("WWHD_ENEMY_TRACE");return v&&*v=='1';}();return on;}
void exec_trace(uint32_t a,const char*path){if(!enemy_trace())return;static std::map<uint32_t,unsigned> n;if(n[a]++%30)return;Entry*e=find(a,false);const uint16_t prof=ld16(a+0xE);const unsigned action=prof==0x7FF9?ld16(a+0x49C+6):prof==0x7FF8?ld16(a+0x8BC+6):prof==0x7FF1?ld16(a+0x590+6):prof==0x7FF7?ld8(a+0x489):prof==0x7FF2?(unsigned(ld8(a+0x3D8))<<8|ld8(a+0x3D9)):prof==0x7FF3?ld8(a+0x3A1):prof==0x7FEA?ld8(a+0x3E0)*100u+ld8(a+0x3E1):prof==0x7FE9?ld8(a+0x3EE)*100u+ld16(a+0x422):ld8(a+0x3FC);  // a rat: its action and mode
 LOG("[native_enemy] trace %s actor=%08X path=%s pos=%.1f %.1f %.1f home=%.1f %.1f %.1f action=%u behaviour=%d verify=%d",e?e->spec.id.c_str():"?",a,path,double(u32_as_f32(ld32(a+0x314))),double(u32_as_f32(ld32(a+0x318))),double(u32_as_f32(ld32(a+0x31C))),double(u32_as_f32(ld32(a+0x2EC))),double(u32_as_f32(ld32(a+0x2F0))),double(u32_as_f32(ld32(a+0x2F4))),action,prof==0x7FF7?int(int8_t(ld8(a+0x48B))):-1,e?int(verify_actor(e->token,e->spec.kind,a)):-1);
 if(prof==0x7FF1)LOG("[native_enemy] trace %s darknut pieces=%X health=%d sword=%08X holding=%u cape=%08X",e?e->spec.id.c_str():"?",unsigned(ld8(a+0x3EC)),int(int8_t(ld8(a+0x3A1))),ld32(a+0x1560),ld32(a+0xD84),ld32(a+0x1598));}
// The live children (sword, cape) a Darknut leaves when it goes: removed with it, so a defeated, removed
// or unloaded Darknut never leaves its pieces behind. Pending ones are refused when they would be made.
std::array<uint32_t,4> live_children(const Entry&e){std::array<uint32_t,4> kids{};unsigned n=0;if(e.spec.kind!=Kind::Darknut||e.pid==0xFFFFFFFFu)return kids;
 story_enemy_ownership::visit_lineage([&](const story_enemy_ownership::LineageInfo&i){if(n<kids.size()&&i.role==story_enemy_ownership::Lineage::EnemyChild&&i.source==e.token&&i.source_pid==e.pid&&i.address&&memory(i.address,0x10)&&ld32(i.address+4)==i.pid&&ld16(i.address+0xE)==i.profile)kids[n++]=i.address;});return kids;}
// test aid: WWHD_TEST_ENEMY_HEALTH=steps:value[,steps:value...] sets an owned enemy's health to value once it
// has run that many execute steps (counted per copy), so a scripted fight needs one landed hit to reach the
// game's own armour-break and defeat paths. Off unless set; never in a normal run.
void test_health(const Entry&e,uint32_t a){static const std::vector<std::pair<unsigned,int>> plan=[]{std::vector<std::pair<unsigned,int>> out;const char*v=std::getenv("WWHD_TEST_ENEMY_HEALTH");while(v&&*v){char*end=nullptr;const unsigned long steps=std::strtoul(v,&end,10);if(end==v||*end!=':')break;v=end+1;const long value=std::strtol(v,&end,10);if(end==v)break;out.push_back({unsigned(steps),int(value)});v=*end==','?end+1:end;if(*end!=',')break;}return out;}();
 if(plan.empty())return;static std::map<uint32_t,unsigned> steps;const unsigned n=++steps[e.pid];for(const auto&p:plan)if(p.first==n){st8(a+0x3A1,uint8_t(int8_t(p.second)));trace(e,a,"test-health",unsigned(p.second));}}
// test aid (with WWHD_ENEMY_TRACE=1): the game's save info (dSv_info_c, 0x12A0 bytes at *0x101F84DC: switches,
// event bits, the zones' actor bits) compared around each owned Darknut create, execute and delete; a change
// is logged with its offset, and the delete logs how many steps were watched
struct SaveWatch{const Entry*e;uint32_t base=0;std::vector<uint8_t> before;static inline unsigned steps=0,changed=0;
 SaveWatch(const Entry*entry,bool on):e(entry){if(!on||!enemy_trace())return;base=ld32(GD(0x101F84DC));if(!memory(base,0x12A0)){base=0;return;}before.assign(static_cast<const uint8_t*>(mem::ptr(base)),static_cast<const uint8_t*>(mem::ptr(base))+0x12A0);}
 void check(const char*what){if(!base)return;++steps;const auto*now=static_cast<const uint8_t*>(mem::ptr(base));for(uint32_t i=0;i<0x12A0;++i)if(now[i]!=before[i]){++changed;LOG("[native_enemy] %s save info changed during %s at +%03X: %02X -> %02X",e->spec.id.c_str(),what,i,unsigned(before[i]),unsigned(now[i]));}}};
// a refused create fails like a retail one (constructed first); a base-only actor skips its own delete
void refuse_or_skip(Cpu*c,uint32_t a,bool create){if(create)native_actor_refusal::refuse(c,a,invoke);else c->r[3]=1;}
void native_delete(Cpu*c,uint32_t fn){if(native_actor_refusal::base_only(c->r[3])){c->r[3]=1;return;}original(c,fn);}
void lifecycle(Cpu*c,uint32_t fn,bool create,bool draw,bool destroy){const uint32_t a=c->r[3];
 // daTn_Create runs daTn_Execute itself (three steps that place the armour pieces): inside its own create
 // the native code runs as it is, under the create's scopes
 if(!create&&!destroy){Entry*n=find(a,false);if(n&&n->creating&&n->spec.kind==Kind::Darknut){original(c,fn);return;}}
 Entry*e=find(a,!create&&!destroy);
 if(!create&&!draw&&!destroy&&enemy_trace()){if(!e)exec_trace(a,find(a,false)?"unverified-original":"not-owned");}if(!e){if(destroy&&cleanup_identity(a,fn)){story_enemy_ownership::retire_before_delete(a);native_delete(c,fn);return;}if(reserved(a)){refuse_or_skip(c,a,create);return;}original(c,fn);return;}
 if(destroy){if(e->spec.kind==Kind::Bokoblin)st8(a+0x137D,0);if(e->spec.kind==Kind::Moblin)st8(a+0x2E0E,0);if(children.retire&&has_children(e->spec.kind))children.retire(e->token,a);const auto kids=live_children(*e);const uint32_t pid=e->pid;story_enemy_ownership::retire_before_delete(a);e->address=0;e->pid=0xFFFFFFFFu;trace(*e,a,"delete");SaveWatch watch(e,e->spec.kind==Kind::Darknut);native_delete(c,fn);const Cpu result=*c;watch.check("delete");if(watch.base)LOG("[native_enemy] %s save info watched over %u Darknut steps: %u changed bytes",e->spec.id.c_str(),SaveWatch::steps,SaveWatch::changed);if(retired)retired(e->token,pid);
  for(uint32_t kid:kids)if(kid){Cpu probe=result;const uint32_t ok=invoke(&probe,GC(0x025D57E0),{kid});trace(*e,a,"child-removed",ok);}
  *c=result;return;}
 if(!supported(e->spec.kind)||e->quarantine){if(!create&&!draw)exec_trace(a,"quarantined");refuse_or_skip(c,a,create);return;}
 if(!create&&story_items::enemy_defeated(e->marker)){if(!draw&&ld8(a+0xC)!=3){Cpu probe=*c;const uint32_t result=invoke(&probe,GC(0x025D57E0),{a});static unsigned reported=0;if(reported++<128)trace(*e,a,"terminal-delete-retry",result);}c->r[3]=1;return;}
 if(!create&&!draw){if(!story_enemy_ownership::lookup_actor(a)){if(!story_enemy_ownership::admit_created(e->token,a)){disable(*e,a);c->r[3]=1;return;}e->address=a;}e->address=a;
  // Carry is refused by the shared player admission hook, never undone here.
  if((ld32(a+0x2E0)&0x2000u)&&!liftable_stone(e->spec,a)){disable(*e,a);trace(*e,a,"unexpected-carried");c->r[3]=1;return;}keep_owned(e->spec,a);
 }
 struct ChildScope {bool active=false;~ChildScope(){if(active)children.leave();}} child_scope;
 if((create||!draw)&&(has_children(e->spec.kind)||(e->spec.kind==Kind::Rat&&(params_of(e->spec)&0x100u)))){  // a bomb rat's bomb is its child
 if(!create&&!children.ready(e->token,a)){if(!draw)exec_trace(a,"children-not-ready");c->r[3]=1;return;}if(!children.enter(e->token,e->spec.kind,a,story_enemy_ownership::source_epoch(e->token))){disable(*e,a);refuse_or_skip(c,a,create);return;}child_scope.active=true;}
 struct HioScope{uint32_t address=0;uint8_t old=0;HioScope(bool active,Kind k){if(active){address=k==Kind::Bokoblin?GD(0x10191468):k==Kind::Moblin?GD(0x101BA690):TNhioSet;old=ld8(address);st8(address,1);}}~HioScope(){if(address)st8(address,old);}} hio(create&&has_children(e->spec.kind),e->spec.kind);
 // daTn_Create rewrites two statics every Darknut shares: search_sp (from the stage name, or 1 for a boss)
 // and s_check (the native boss-Darknut event counter). A mod Darknut leaves both as they were.
 struct TnStatics{bool active;uint8_t sp=0,check=0;TnStatics(bool on):active(on){if(on){sp=ld8(TNsearchSp);check=ld8(TNsCheck);}}~TnStatics(){if(active){st8(TNsearchSp,sp);st8(TNsCheck,check);}}} tn_statics(create&&e->spec.kind==Kind::Darknut);
 struct Creating{Entry*entry;Creating(Entry*e,bool on):entry(on?e:nullptr){if(entry)entry->creating=true;}~Creating(){if(entry)entry->creating=false;}} creating(e,create);
 if(!create&&!draw){exec_trace(a,"execute");test_health(*e,a);}
 SaveWatch watch(e,e->spec.kind==Kind::Darknut&&!draw);
 Scope scope(*e);if(fn==NGexec)c->r[3]=1;else original(c,fn);const Cpu result=*c;  // an owned rat hole never spawns on its own
 watch.check(create?"create":"execute");
 // a Bubble's grapple steal (stolenItemCount 0x406, copied into the steal count on a hookshot hit) is off
 if(create&&result.r[3]==4&&e->spec.kind==Kind::Bubble)st8(a+0x406,0);
 if(create&&result.r[3]==4){if(!verify_actor(e->token,e->spec.kind,a)||(!story_enemy_ownership::lookup_actor(a)&&!story_enemy_ownership::admit_created(e->token,a))){disable(*e,a);c->r[3]=5;return;}e->address=a;if(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin){e->search_mode=ld8(GD(e->spec.kind==Kind::Bokoblin?0x1046232D:0x104654A4));e->search_known=true;}no_carry(a);trace(*e,a,"created");}
 else if(create&&result.r[3]==5){disable(*e,a);trace(*e,a,"create-failed");}
 if(!draw&&verify_actor(e->token,e->spec.kind,a))keep_owned(e->spec,a);*c=result;
}
void cc_create(Cpu*c){lifecycle(c,CCcreate,true,false,false);}void cc_exec(Cpu*c){lifecycle(c,CCexec,false,false,false);}void cc_draw(Cpu*c){lifecycle(c,CCdraw,false,true,false);}void cc_delete(Cpu*c){lifecycle(c,CCdelete,false,false,true);}
void ki_create(Cpu*c){lifecycle(c,KIcreate,true,false,false);}void ki_exec(Cpu*c){lifecycle(c,KIexec,false,false,false);}void ki_draw(Cpu*c){lifecycle(c,KIdraw,false,true,false);}void ki_delete(Cpu*c){lifecycle(c,KIdelete,false,false,true);}
void bk_create(Cpu*c){lifecycle(c,BKcreate,true,false,false);}void bk_exec(Cpu*c){lifecycle(c,BKexec,false,false,false);}void bk_draw(Cpu*c){lifecycle(c,BKdraw,false,true,false);}void bk_delete(Cpu*c){lifecycle(c,BKdelete,false,false,true);}
void pt_create(Cpu*c){lifecycle(c,PTcreate,true,false,false);}void pt_exec(Cpu*c){lifecycle(c,PTexec,false,false,false);}void pt_draw(Cpu*c){lifecycle(c,PTdraw,false,true,false);}void pt_delete(Cpu*c){lifecycle(c,PTdelete,false,false,true);}
void ph_create(Cpu*c){lifecycle(c,PHcreate,true,false,false);}void ph_exec(Cpu*c){lifecycle(c,PHexec,false,false,false);}void ph_draw(Cpu*c){lifecycle(c,PHdraw,false,true,false);}void ph_delete(Cpu*c){lifecycle(c,PHdelete,false,false,true);}
// the owned rat hole: its own spawning (daNZG_Execute) never runs; the port creates its rats (story_actors)
void ng_create(Cpu*c){lifecycle(c,NGcreate,true,false,false);}void ng_draw(Cpu*c){lifecycle(c,NGdraw,false,true,false);}void ng_delete(Cpu*c){lifecycle(c,NGdelete,false,false,true);}
void ng_exec(Cpu*c){lifecycle(c,NGexec,false,false,false);}
void nz_create(Cpu*c){lifecycle(c,NZcreate,true,false,false);}void nz_exec(Cpu*c){lifecycle(c,NZexec,false,false,false);}void nz_draw(Cpu*c){lifecycle(c,NZdraw,false,true,false);}void nz_delete(Cpu*c){lifecycle(c,NZdelete,false,false,true);}
void tn_create(Cpu*c){lifecycle(c,TNcreate,true,false,false);}void tn_exec(Cpu*c){lifecycle(c,TNexec,false,false,false);}void tn_draw(Cpu*c){lifecycle(c,TNdraw,false,true,false);}void tn_delete(Cpu*c){lifecycle(c,TNdelete,false,false,true);}
void bo_create(Cpu*c){lifecycle(c,BOcreate,true,false,false);}void bo_exec(Cpu*c){lifecycle(c,BOexec,false,false,false);}void bo_draw(Cpu*c){lifecycle(c,BOdraw,false,true,false);}void bo_delete(Cpu*c){lifecycle(c,BOdelete,false,false,true);}
void bl_create(Cpu*c){lifecycle(c,BLcreate,true,false,false);}void bl_exec(Cpu*c){lifecycle(c,BLexec,false,false,false);}void bl_draw(Cpu*c){lifecycle(c,BLdraw,false,true,false);}void bl_delete(Cpu*c){lifecycle(c,BLdelete,false,false,true);}
void mo_create(Cpu*c){lifecycle(c,MOcreate,true,false,false);}void mo_exec(Cpu*c){lifecycle(c,MOexec,false,false,false);}void mo_draw(Cpu*c){lifecycle(c,MOdraw,false,true,false);}void mo_delete(Cpu*c){lifecycle(c,MOdelete,false,false,true);}
void disappear(Cpu*c){const uint32_t a=c->r[3];Entry*e=find(a);if(!e){if(reserved(a)){c->r[3]=0xFFFFFFFFu;return;}original(c,Disappear);return;}
 const bool terminal=e->spec.kind==Kind::ChuChu?((ld8(a+0x3FC)==4&&(ld8(a+0x3FD)==51||ld8(a+0x3FD)==53))||(liftable_stone(e->spec,a)&&ld8(a+0x3FD)>=0x52&&ld8(a+0x3FD)<=0x54)):e->spec.kind==Kind::Bokoblin?ld16(a+0x49C+6)==20:e->spec.kind==Kind::Moblin?ld16(a+0x8BC+6)==20:e->spec.kind==Kind::Darknut?ld16(a+0x590+6)==20:e->spec.kind==Kind::Miniblin?(int8_t(ld8(a+0x3A1))<=0||ld16(a+0x3EE)==4||ld16(a+0x3EE)==5):e->spec.kind==Kind::Peahat?int8_t(ld8(a+0x3A1))<=0:e->spec.kind==Kind::Rat?ld8(a+0x3D9)==57:
  // Boko Baba: action 2 (dying), mode 21, the stem left (kind byte 1) at the drop frame
  e->spec.kind==Kind::BokoBaba?(ld8(a+0x3E0)==2&&ld8(a+0x3E1)==21&&ld8(a+0x3DC)==1):
  // Bubble: a burning red one put down (retreat step 10), or either colour's skull at step 42, health gone
  e->spec.kind==Kind::Bubble?(int8_t(ld8(a+0x3A1))<=0&&((ld8(a+0x3EE)==1&&ld16(a+0x422)==10&&ld8(a+0x3EF)==1&&ld8(a+0x3EC)==0)||(ld8(a+0x3EE)==4&&ld16(a+0x422)==42))):ld8(a+0x489)==30;
 if(terminal)claim(*e,a);else {disable(*e,a);trace(*e,a,"unexpected-drop");}
 // Suppress the asynchronous retail drop actor completely: it otherwise loses
 // the source lifetime and can award native inventory after the enemy is gone.
 c->r[3]=0xFFFFFFFFu;
}
void ice(Cpu*c){const uint32_t helper=c->r[3];if(!memory(helper,4)){original(c,Ice);return;}const uint32_t a=ld32(helper);Entry*e=find(a);if(!e){if(reserved(a)){c->r[3]=0;return;}original(c,Ice);return;}
 if(executing!=e||helper!=a+ice_offset(e->spec.kind)||ld8(helper+0x1B1)!=0){disable(*e,a);c->r[3]=0;return;}
 struct Guard{Entry*old;Guard(Entry*e):old(ice_owner){ice_owner=e;}~Guard(){ice_owner=old;}} guard(e);
 original(c,Ice);const Cpu result=*c;no_carry(a);*c=result;
}
void table_item(Cpu*c){Entry*e=ice_owner;if(e&&e==executing&&e->address&&verify_actor(e->token,e->spec.kind,e->address)&&c->r[6]==e->address+0x320&&c->r[4]==ld32(e->address+0x3A4)){claim(*e,e->address);c->r[3]=0xFFFFFFFFu;return;}if(e){disable(*e,e->address);c->r[3]=0xFFFFFFFFu;return;}original(c,TableItem);}
void ki_search(Cpu*c){const uint32_t a=c->r[3];if(executing&&executing->spec.kind==Kind::Keese){Entry*e=find(a);if(e&&!e->quarantine&&e->spec.kind==Kind::Keese&&e->spec.mod==executing->spec.mod&&story_enemy_ownership::lookup_actor(a)){st32(GD(0x10464B28),ld32(GD(0x10464B28))+1);if(ld8(a+0x489)==10&&int8_t(ld8(a+0x48B))<10)st32(GD(0x10464B2C),ld32(GD(0x10464B2C))+1);}c->r[3]=0;return;}original(c,KiSearch);}
void mo_capture(Cpu*c){if(auto*e=find(c->r[3])){if(e->spec.kind==Kind::Moblin){trace(*e,c->r[3],"capture-denied");return;}}if(reserved(c->r[3]))return;original(c,GC(0x021C9AA0));}
// The Boko Baba's bite asks for an event to grab and chew Link (Link held in a demo, an event camera, both ended
// only by its own later steps). An owned one never grabs: the request is refused and its grab latch (0x3E7) cleared,
// so it goes on biting (the bite's damage stays). Any other owned request quarantines the enemy.
void potential_event(Cpu*c){if(auto*e=find(c->r[3])){if(e->spec.kind==Kind::BokoBaba&&executing==e&&!ld8(c->r[3]+0x3E8)){st8(c->r[3]+0x3E7,0);c->r[3]=0;return;}disable(*e,c->r[3]);trace(*e,c->r[3],"event-denied");c->r[3]=0;return;}if(reserved(c->r[3])){c->r[3]=0;return;}original(c,GC(0x025D7B24));}
void carry_drop(Cpu*c,uint32_t fn){if(auto*e=find(c->r[3])){disable(*e,c->r[3]);trace(*e,c->r[3],"carry-drop-denied");return;}if(reserved(c->r[3]))return;original(c,fn);}
void bk_carry_drop(Cpu*c){carry_drop(c,GC(0x020A47B4));}void mo_carry_drop(Cpu*c){carry_drop(c,GC(0x021D0294));}
void water_fail(Cpu*c){const uint32_t a=c->r[3];auto*e=find(a);if(!e){if(reserved(a))return;original(c,GC(0x020A3B18));return;}if(e->spec.kind!=Kind::Bokoblin||executing!=e||ld16(a+0x49C+6)!=22){disable(*e,a);return;}if(ld16(a+0x49C+4)==1&&ld16(a+0x418+6)==0)claim(*e,a);original(c,GC(0x020A3B18));}
void fire(Cpu*c){const uint32_t h=c->r[3];if(!memory(h,4)){original(c,GC(0x02041570));return;}const uint32_t a=ld32(h);auto*e=find(a);if(!e){if(reserved(a))return;original(c,GC(0x02041570));return;}const uint32_t offset=e->spec.kind==Kind::ChuChu?0xCDC:e->spec.kind==Kind::Bokoblin?0x1774:e->spec.kind==Kind::Moblin?0x320C:e->spec.kind==Kind::Miniblin?0xCB4:e->spec.kind==Kind::Peahat?0xE6C:e->spec.kind==Kind::Rat?0xF98:e->spec.kind==Kind::Darknut?0x1A2C:e->spec.kind==Kind::BokoBaba?0xED0:e->spec.kind==Kind::Bubble?0xBE4:0xE90;if(executing!=e||(h!=a+offset&&!(e->spec.kind==Kind::Peahat&&h==a+0x1098))){disable(*e,a);return;}original(c,GC(0x02041570));}
void reaction(Cpu*c,uint32_t fn){const uint32_t h=c->r[3];if(!memory(h,4)){original(c,fn);return;}const uint32_t a=ld32(h);auto*e=find(a);if(!e){if(reserved(a)){c->r[3]=0;return;}original(c,fn);return;}const uint32_t offset=e->spec.kind==Kind::Bokoblin?0x49C:e->spec.kind==Kind::Moblin?0x8BC:e->spec.kind==Kind::Darknut?0x590:0;const unsigned type=e->spec.kind==Kind::Bokoblin?2:e->spec.kind==Kind::Darknut?3:1;if(!offset||h!=a+offset||executing!=e||ld16(h+8)!=type){disable(*e,a);c->r[3]=0;return;}if(fn==GC(0x02043F34)){DrowningScope current{e,h,false};struct Guard{DrowningScope*prior;Guard(DrowningScope*p):prior(drowning){drowning=p;}~Guard(){drowning=prior;}}guard(&current);original(c,fn);const Cpu result=*c;if(current.queued&&find(a)==e&&ld8(a+0xC)==3)claim(*e,a);*c=result;}else original(c,fn);}
void delete_request(Cpu*c){const uint32_t a=c->r[3],lr=c->lr;auto*context=drowning;Entry*e=context?context->entry:nullptr;const bool exact=e&&e==executing&&e->spec.kind==Kind::Bokoblin&&context->helper==a+0x49C&&find(a)==e&&ld32(context->helper)==a&&ld16(context->helper+8)==2&&ld8(a+0x3D4)==0&&lr==GC(0x02044FCC)&&story_enemy_ownership::lookup_actor(a)==e->token;original(c,GC(0x025D57E0));if(exact&&c->r[3]==1&&ld8(a+0xC)==3)context->queued=true;}
void damage_reaction(Cpu*c){reaction(c,GC(0x02041F94));}void damage_set(Cpu*c){reaction(c,GC(0x02043F34));}
void room_search(Cpu*c){if(executing){auto*e=find(c->r[3]);c->r[3]=e&&!e->quarantine&&e->spec.mod==executing->spec.mod?c->r[3]:0;return;}if(reserved(c->r[3])){c->r[3]=0;return;}original(c,RoomSearch);}
}
const char* kind_name(Kind k){return k==Kind::ChuChu?"chuchu":k==Kind::Bokoblin?"bokoblin":k==Kind::Moblin?"moblin":k==Kind::Keese?"keese":k==Kind::Miniblin?"miniblin":k==Kind::Peahat?"peahat":k==Kind::Rat?"rat":k==Kind::Darknut?"darknut":k==Kind::RatHole?"rat_hole":k==Kind::BokoBaba?"bokobaba":k==Kind::Bubble?"bubble":"";}
uint32_t parameters(Kind k,const std::string& variant){if(!story_enemy_variants::has_variants(kind_name(k)))return variant.empty()?parameters(k):0u;const auto*v=story_enemy_variants::find(kind_name(k),variant);return v?v->params:0u;}
uint32_t parameters(Token t){const auto*e=get(t);return e?params_of(e->spec):0u;}
uint16_t home_x(Token t){const auto*e=get(t);return e&&e->spec.kind==Kind::Darknut?story_enemy_variants::angle_x("darknut",params_of(e->spec)):0;}
bool configure_children(ChildBridge b,bool verified){if(installed)return false;if(verified&&(!b.enter||!b.leave||!b.ready||!b.retire))return false;children=std::move(b);weapon_admission=verified;return true;}
void child_retired(const story_enemy_ownership::LineageInfo&info){if(!info.source_valid||info.role!=story_enemy_ownership::Lineage::EnemyChild)return;auto*e=get(info.source);if(!e||!has_children(e->spec.kind)||e->pid!=info.source_pid||!info.source_actor||find(info.source_actor)!=e||story_enemy_ownership::lookup_actor(info.source_actor)!=e->token||story_enemy_ownership::source_epoch(e->token)!=info.epoch)return;const uint32_t a=info.source_actor;
 // a Darknut's sword (mBokoPcId 0x1560, with m0C34/m0C38/m140C its holding state) or cape (mMantPcId 0x1598)
 if(e->spec.kind==Kind::Darknut){if(ld32(a+0x1560)==info.pid){st32(a+0x1560,0xFFFFFFFFu);st32(a+0xD84,0);st32(a+0xD88,0);st8(a+0x155C,0);}else if(ld32(a+0x1598)==info.pid)st32(a+0x1598,0xFFFFu);else return;trace(*e,a,"child-retired-native-loss",info.pid);return;}
 const uint32_t reference=e->spec.kind==Kind::Bokoblin?0x1360:0x2D60;if(ld32(a+reference)!=info.pid)return;
 if(e->spec.kind==Kind::Bokoblin){st32(a+0xC90,0);st32(a+0xC94,0);st8(a+0x1374,0);}else{st8(a+0x10B3,0);}
 st32(a+reference,0xFFFFFFFFu);trace(*e,a,"child-retired-native-loss",info.pid);}
bool supported(Kind k){return k==Kind::ChuChu||k==Kind::Keese||k==Kind::Miniblin||k==Kind::Peahat||k==Kind::Rat||k==Kind::Bubble||k==Kind::BokoBaba||k==Kind::RatHole||(weapon_admission&&has_children(k));}
bool verify_actor(Token t,Kind kind,uint32_t a){const auto*e=get(t);const auto&d=story_enemy_ownership::descriptor(kind);if(!e||kind!=e->spec.kind||!supported(kind)||!memory(a,d.bytes)||ld16(a+0xE)!=d.profile||ld32(a+0xB4)!=native_vtable(kind)||ld32(a+0xB0)!=params_of(e->spec)||ld8(a+0x2DD)!=0xFF||ld16(a+0x2D8)!=0xFFFF||ld16(a+0x2FC)!=home_z(kind))return false;
 if(kind==Kind::ChuChu)return ld8(a+0x3F8)==0&&ld8(a+0x3F9)==((params_of(e->spec)>>8)&0xFF)&&ld8(a+0x3FA)==0&&ld8(a+0x3FB)==0xFF&&(ld8(a+0x3FC)<=4||(black_chuchu(e->spec)&&ld8(a+0x3FC)==6))&&ld8(a+0x924+0x1B1)==0;
 if(kind==Kind::Bokoblin){const unsigned action=ld16(a+0x49C+6);const uint32_t p=params_of(e->spec);return ld8(a+0x3D0)==(p&0xF)&&ld8(a+0x3D1)==0xFF&&ld8(a+0x3D2)==((p>>16)&0xFF)&&ld8(a+0x3D3)==0xFF&&ld8(a+0x3D4)==0&&ld8(a+0x3D5)==0&&ld8(a+0x3D6)==0&&ld8(a+0x3F0)==(params_of(e->spec)&0x20u)&&ld8(a+0x3F1)==(params_of(e->spec)&0x40u)&&ld8(a+0x3F8)==0&&ld32(a+0x1378)==patrol_route(*e)&&(ld8(a+0x1375)==0||(story_enemy_variants::bokoblin_patrols(p)&&ld8(a+0x1375)==((story_enemy_variants::kPatrolRoute+1)&0xFF)))&&ld32(a+0x1380)==0&&ld8(a+0x138D)==0&&ld8(a+0x137D)==0&&action<=23&&action!=15&&action!=16&&ld8(a+0x13BC+0x1B1)==0;}
 if(kind==Kind::Moblin){const unsigned action=ld16(a+0x8BC+6);const uint32_t p=params_of(e->spec);const bool patrol=story_enemy_variants::patrol_word(p);return ld8(a+0x3D8)==0&&ld8(a+0x3D9)==0&&ld8(a+0x3DA)==((p>>16)&0xFF)&&ld8(a+0x3DB)==0xFF&&ld8(a+0x3DC)==0&&ld8(a+0x3DD)==0&&(ld8(a+0x2D70)==0||(patrol&&ld8(a+0x2D70)==((story_enemy_variants::kPatrolRoute+1)&0xFF)))&&(ld8(a+0x2D72)==0||(patrol&&(ld8(a+0x2D72)==1||ld8(a+0x2D72)==0xFF)))&&ld32(a+0x2D74)==patrol_route(*e)&&ld16(a+0x2E4C)==0&&ld8(a+0x2E4E)==0&&ld16(a+0x2E50)==0&&ld8(a+0x2E21)==0&&ld8(a+0x2E0E)==0&&!(ld32(a+0x2E0)&0x04000000u)&&action<=23&&action!=6&&ld8(a+0x2E54+0x1B1)==0;}
 if(kind==Kind::Rat)return ld8(a+0x3D0)==1&&ld8(a+0x3D1)==((params_of(e->spec)>>8)&0xFF)&&ld8(a+0xBE0+0x1B1)==0;  // hole-born (arg0 1), its own type
 if(kind==Kind::RatHole)return ld8(a+0x3D4)==0&&ld8(a+0x3D5)==1&&ld8(a+0x3D6)==1&&ld8(a+0x405)==0xFF&&ld32(a+0x3F4)==0;  // no range, one rat, no shop keeper, no path
 if(kind==Kind::Peahat)return ld8(a+0x45A)==(params_of(e->spec)&0xFF)&&ld8(a+0xAB4+0x1B1)==0;  // its declared type (Peahat 0, Seahat 1), unfrozen
 // Boko Baba: the ordinary one (variant 0: never a Baba Bud), never holding Link, the stem only while it dies, unfrozen
 if(kind==Kind::BokoBaba)return ld8(a+0x3DD)==0&&(ld8(a+0x3DC)==0||(ld8(a+0x3DC)==1&&ld8(a+0x3E0)==2))&&ld8(a+0x3E8)==0&&ld8(a+0xB18+0x1B1)==0;
 // Bubble: its declared colour (0 red, 1 blue), on the ground, no switch, no path, nothing to steal, unfrozen
 if(kind==Kind::Bubble)return ld8(a+0x3EC)==(params_of(e->spec)&0xFF)&&ld8(a+0x3ED)==0&&ld8(a+0x3F1)==0xFF&&ld8(a+0x405)==0xFF&&ld32(a+0x3F4)==0&&ld8(a+0x406)==0&&ld8(a+0x82C+0x1B1)==0;
 // Darknut: guard behaviour 4, the variant's sight and colour, no path (pointer and index), no spawn or defeat
 // switch (the ice death switch too), the variant's equipment in place and in its x rotation word, never the
 // HIO owner, never a boss (no event camera or music), and an action short of the event-only ones (22: in deep
 // water, where the game leaves a Darknut standing: not a defeat, it comes back after a room change)
 if(kind==Kind::Darknut){const uint32_t p=params_of(e->spec);const uint16_t ax=story_enemy_variants::angle_x("darknut",p);const unsigned action=ld16(a+0x590+6);
  return story_enemy_variants::declared_params("darknut",p)&&ld8(a+0x3D8)==4&&ld8(a+0x3D9)==((p>>8)&0xFF)&&ld8(a+0x3DA)==0xFF&&ld8(a+0x3DB)==0xFF&&ld8(a+0x3DC)==0&&ld8(a+0x3DD)==0&&ld8(a+0x4DC)==((p>>4)&0xF)&&ld8(a+0x4DD)==((ax>>5)&7)&&ld16(a+0x2F8)==ax&&ld32(a+0x1578)==0&&ld8(a+0x1574)==0&&ld8(a+0x1582)==0&&ld8(a+0x1648)==0&&!(ld32(a+0x2E0)&0x04000000u)&&action<=22&&ld8(a+0x1674+0x1B1)==0;}
 if(kind==Kind::Miniblin)return ld8(a+0x3D0)==1&&ld8(a+0x3D1)==10&&ld8(a+0x3D2)==0&&ld8(a+0x3D3)==0xFF&&ld8(a+0x3D4)==0xFF&&ld8(a+0x3D6)==0&&ld8(a+0x8FC+0x1B1)==0;
 // Keese: mDamageType 0xAB9 is 0x80 for the Fire Keese variant (parameter bit 15), 0 for the ordinary one
 return ld8(a+0x484)==0&&ld8(a+0x485)==0&&ld8(a+0x486)==0xFF&&ld8(a+0x487)==0xFF&&ld8(a+0x48C)==0&&ld8(a+0x48D)==0&&ld32(a+0x490)==0&&(ld8(a+0x489)==0||ld8(a+0x489)==10||ld8(a+0x489)==20||ld8(a+0x489)==30)&&ld8(a+0xAB9)==((params_of(e->spec)>>8)&0x80u)&&ld8(a+0xAD8+0x1B1)==0;
}
Token declare(const Spec&s,std::string&why){if(installed||!supported(s.kind)||s.mod.empty()||s.id.empty()||s.defeated_flag.empty()||!s.epoch||entries.size()>=128){why="Unsupported or invalid native enemy declaration";return 0;}for(const auto&e:entries)if(e.spec.mod==s.mod&&(e.spec.id==s.id||e.spec.defeated_flag==s.defeated_flag)){why="Duplicate native enemy identity or defeat flag";return 0;}if(s.drops.size()>8){why="Too many native enemy drops";return 0;}for(size_t i=0;i<s.drops.size();++i){const auto&drop=s.drops[i];if(!drop.quantity||drop.quantity>254||!story_items::find(s.mod,drop.item)){why="Unavailable same-mod enemy item";return 0;}for(size_t j=0;j<i;++j)if(s.drops[j].item==drop.item){why="Duplicate native enemy item";return 0;}}
 auto marker=story_items::register_enemy_marker(s.mod,s.id,s.defeated_flag,why);if(!marker)return 0;Token t=story_enemy_ownership::declare_actor(s.kind,params_of(s),s.fingerprint,s.epoch,why);if(!t)return 0;entries.push_back({s,t,marker});return t;}
size_t checkpoint(){return entries.size();}void rollback(size_t n)noexcept{if(!installed&&n<=entries.size())entries.resize(n);}
void editor_enable(){editor_allowed=true;}
bool live(){return installed&&editor_allowed;}
Token editor_declare(const Spec&s,std::string&why){if(!live()){why="an enemy appears after a restart";return 0;}installed=false;struct Back{~Back(){installed=true;}}back;const Token t=declare(s,why);if(t)LOG("[native_enemy] %s:%s declared by the editor (token %u)",s.mod.c_str(),s.id.c_str(),t);return t;}
bool editor_redeclare(Token t,const Spec&s,std::string&why){auto*e=get(t);if(!live()||!e){why="the enemy takes this change after a restart";return false;}
 if(s.kind!=e->spec.kind||s.mod!=e->spec.mod){why="another kind of enemy is a new declaration";return false;}if(e->pid!=0xFFFFFFFFu||e->address){why="the enemy is still in the game";return false;}
 if(s.drops.size()>8){why="Too many native enemy drops";return false;}for(size_t i=0;i<s.drops.size();++i){if(!s.drops[i].quantity||s.drops[i].quantity>254||!story_items::find(s.mod,s.drops[i].item)){why="Unavailable same-mod enemy item";return false;}for(size_t j=0;j<i;++j)if(s.drops[j].item==s.drops[i].item){why="Duplicate native enemy item";return false;}}
 for(const auto&o:entries)if(o.token!=t&&o.spec.mod==s.mod&&(o.spec.id==s.id||o.spec.defeated_flag==s.defeated_flag)){why="Duplicate native enemy identity or defeat flag";return false;}
 auto marker=e->marker;
 if(s.defeated_flag!=e->spec.defeated_flag||s.id!=e->spec.id){story_items::forget_prop_marker(e->marker.value);marker=story_items::register_enemy_marker(s.mod,s.id,s.defeated_flag,why);if(!marker.value){e->marker=story_items::register_enemy_marker(e->spec.mod,e->spec.id,e->spec.defeated_flag,why);return false;}}
 if(params_of(s)!=params_of(e->spec)&&!story_enemy_ownership::editor_reparameter(t,params_of(s))){why="the enemy's ownership record is gone";return false;}
 if(!story_enemy_ownership::editor_refingerprint(t,s.fingerprint)){why="the enemy's ownership record is gone";return false;}
 const uint32_t epoch=e->spec.epoch;e->spec=s;e->spec.epoch=epoch;e->marker=marker;e->quarantine=false;return true;}
bool editor_refingerprint(Token t,const story_enemy_ownership::Fingerprint&fp){auto*e=get(t);if(!e||!story_enemy_ownership::editor_refingerprint(t,fp))return false;e->spec.fingerprint=fp;return true;}
void bind(Token t,uint32_t pid,bool restored){if(auto*e=get(t)){if(!restored&&!story_enemy_ownership::bind_pending(t,pid)){disable(*e,e->address);return;}e->pid=pid;e->address=0;e->quarantine=false;}}
namespace {bool routes_installed=false;const uint32_t RoomPath=GC(0x025AAF88);
void room_path(Cpu*c){Entry*e=executing;if(e&&(e->spec.kind==Kind::Bokoblin||e->spec.kind==Kind::Moblin)&&story_enemy_variants::patrol_word(params_of(e->spec))&&c->r[3]==story_enemy_variants::kPatrolRoute&&route_provider){const uint32_t rec=route_provider(e->token);if(rec){c->r[3]=rec;return;}c->r[3]=0;return;}original(c,RoomPath);}}
void set_route_provider(std::function<uint32_t(Token)> route){route_provider=std::move(route);}
uint32_t room_path_function(){return RoomPath;}
bool install_routes(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(routes_installed)return true;if(!original||!owner(RoomPath).empty())return false;replace(RoomPath,room_path);routes_installed=true;return true;}
bool is_defeated(Token t){auto*e=get(t);return e&&story_items::enemy_defeated(e->marker);}
// an enemy the port retires as defeated without its own defeat (a closed rat hole): its marker, no drops;
// its actor removes itself on its next step like any defeated one
bool settle(Token t){auto*e=get(t);if(!e||e->quarantine||story_items::enemy_defeated(e->marker))return false;const auto result=story_items::claim_enemy_drops(e->marker,e->spec.mod,{});if(e->address)trace(*e,e->address,"settle",unsigned(result));return result==story_items::EnemyClaimStatus::Committed;}
bool available(Token t){auto*e=get(t);return e&&!e->quarantine&&!story_items::enemy_defeated(e->marker);}
void quarantine(Token t){if(auto*e=get(t))disable(*e,e->address);}
bool begin_epoch(Token t,uint32_t epoch){auto*e=get(t);if(!e||!story_enemy_ownership::begin_epoch(t,epoch))return false;e->spec.epoch=epoch;e->pid=0xFFFFFFFFu;e->address=0;e->quarantine=false;return true;}
void state_loaded(){executing=ice_owner=nullptr;drowning=nullptr;for(auto&e:entries){e.address=0;e.pid=0xFFFFFFFFu;e.quarantine=false;e.claiming=false;}}
bool owns_actor(uint32_t a){return find(a)!=nullptr;}
bool deny_create(uint32_t,uint32_t){return executing&&executing->spec.kind==Kind::BokoBaba;}
bool deny_carry(uint32_t a){const Entry*e=find(a,false);return e&&!liftable_stone(e->spec,a);}
void configure(void(*o)(Cpu*,uint32_t),uint32_t(*call)(Cpu*,uint32_t,std::initializer_list<uint32_t>),const std::array<uint32_t,story_enemy_ownership::kKindCount>&storage,std::function<void(Token,uint32_t)>notify){original=o;invoke=call;profiles=storage;retired=std::move(notify);native_actor_profiles::configure(o,call);}
bool install(const std::function<std::string(uint32_t)>&owner,const std::function<void(uint32_t,void(*)(Cpu*))>&replace){if(installed)return true;if((entries.empty()&&!editor_allowed)||!original||!invoke||!native_actor_profiles::available(owner))return false;
 const std::pair<uint32_t,void(*)(Cpu*)>hooks[]={{BKcreate,bk_create},{BKexec,bk_exec},{BKdraw,bk_draw},{BKdelete,bk_delete},{MOcreate,mo_create},{MOexec,mo_exec},{MOdraw,mo_draw},{MOdelete,mo_delete},{CCcreate,cc_create},{CCexec,cc_exec},{CCdraw,cc_draw},{CCdelete,cc_delete},{KIcreate,ki_create},{KIexec,ki_exec},{KIdraw,ki_draw},{KIdelete,ki_delete},{PTcreate,pt_create},{PTexec,pt_exec},{PTdraw,pt_draw},{PTdelete,pt_delete},{TNcreate,tn_create},{TNexec,tn_exec},{TNdraw,tn_draw},{TNdelete,tn_delete},{PHcreate,ph_create},{PHexec,ph_exec},{PHdraw,ph_draw},{PHdelete,ph_delete},{NZcreate,nz_create},{NZexec,nz_exec},{NZdraw,nz_draw},{NZdelete,nz_delete},{NGcreate,ng_create},{NGexec,ng_exec},{NGdraw,ng_draw},{NGdelete,ng_delete},{BOcreate,bo_create},{BOexec,bo_exec},{BOdraw,bo_draw},{BOdelete,bo_delete},{BLcreate,bl_create},{BLexec,bl_exec},{BLdraw,bl_draw},{BLdelete,bl_delete},{Disappear,disappear},{Ice,ice},{TableItem,table_item},{KiSearch,ki_search},{RoomSearch,room_search},{GC(0x021C9AA0),mo_capture},{GC(0x025D7B24),potential_event},{GC(0x020A47B4),bk_carry_drop},{GC(0x021D0294),mo_carry_drop},{GC(0x020A3B18),water_fail},{GC(0x02041570),fire},{GC(0x02041F94),damage_reaction},{GC(0x02043F34),damage_set},{GC(0x025D57E0),delete_request}};
 for(const auto&h:hooks)if(!owner(h.first).empty())return false;
 for(Kind k:kKinds){if(!supported(k))continue;const auto&d=story_enemy_ownership::descriptor(k);uint32_t storage=profiles[unsigned(k)],native=ld32(GD(0x101F3EE0)+4*d.native_profile);if(!native_actor_profiles::valid_storage(storage)||!memory(native,0x30)||ld32(native+0x10)!=d.bytes)return false;}
 for(Kind k:kKinds){if(!supported(k))continue;const auto&d=story_enemy_ownership::descriptor(k);uint32_t storage=profiles[unsigned(k)],native=ld32(GD(0x101F3EE0)+4*d.native_profile);std::memcpy(mem::ptr(storage),mem::ptr(native),0x30);st16(storage+8,d.profile);if(!native_actor_profiles::add(d.profile,storage))return false;}
 if(!native_actor_profiles::install(owner,replace))return false;for(const auto&h:hooks)replace(h.first,h.second);installed=true;return true;
}
}

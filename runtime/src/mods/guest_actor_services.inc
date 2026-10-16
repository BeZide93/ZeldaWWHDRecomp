// Internal guest_mods.cpp implementation; Loaded/owner and bounded strings are
// defined by the including translation unit. All dispatch is on the game thread.
mod_actor::behavior::Registry g_actor_registry;
std::map<uint32_t,mod_actor::Identity> g_actor_identities;
thread_local bool g_actor_draw_callback=false;

bool actor_owned_function(const std::string& owner_id,uint32_t function){
    for(const auto& mod:g_loaded)if(mod.id==owner_id)
        for(uint32_t i=0;i<mod.m->func_count;++i)if(mod.m->funcs[i].addr==function)return true;
    return false;
}
void actor_sweep(Cpu* c){
    for(auto it=g_actor_identities.begin();it!=g_actor_identities.end();){
        if(!mod_actor::resolve(c,it->second)){g_actor_registry.invalidate(it->first);it=g_actor_identities.erase(it);}
        else ++it;
    }
}
std::optional<mod_actor::behavior::Binding> actor_binding(Cpu* c,uint32_t handle,const std::string& owner_id){
    const auto it=g_actor_identities.find(handle);if(it==g_actor_identities.end())return {};
    const auto identity=it->second;
    return g_actor_registry.resolve(handle,owner_id,[&](const auto& native){return native.process_id==identity.process_id&&native.declaration_token==identity.token&&identity.mod==owner_id&&mod_actor::resolve(c,identity)!=0;});
}
bool actor_buffer(Cpu* c,const Loaded& mod,uint32_t address,uint32_t bytes){
    if(!address||uint64_t(address)+bytes>0x100000000ull)return false;
    if(setting_buffer(mod,address,bytes))return true;
    for(const auto& [handle,identity]:g_actor_identities){
        if(identity.mod!=mod.id)continue;const auto binding=actor_binding(c,handle,mod.id);if(!binding)continue;
        const uint32_t begin=mod_actor::userdata_address(c,identity);
        if(begin&&address>=begin&&uint64_t(address)+bytes<=uint64_t(begin)+binding->userdata_size)return true;
    }
    return false;
}
bool actor_dispatch(Cpu* c,uint32_t handle,mod_actor::behavior::Event event,uint32_t argument=0){
    const auto it=g_actor_identities.find(handle);if(it==g_actor_identities.end())return false;
    const auto identity=it->second;
    const auto dispatch=g_actor_registry.begin_callback(handle,identity.mod,event,[&](const auto& native){return native.process_id==identity.process_id&&native.declaration_token==identity.token&&mod_actor::resolve(c,identity)!=0;},actor_owned_function);
    if(!dispatch)return actor_binding(c,handle,identity.mod).has_value(); // optional callback
    const uint32_t userdata=dispatch->binding.userdata_size?mod_actor::userdata_address(c,identity):0;
    if(dispatch->binding.userdata_size&&!userdata){g_actor_registry.finish_callback(dispatch->ticket,false);return false;}
    const Cpu saved=*c;const bool prior_draw=g_actor_draw_callback;g_actor_draw_callback=prior_draw||event==mod_actor::behavior::Event::Draw;
    bool success=true;
    try{
        if(event==mod_actor::behavior::Event::Execute){c->f[1].ps0=1.0/30.0;guest_call(c,dispatch->function,{handle,userdata});}
        else if(event==mod_actor::behavior::Event::Create||event==mod_actor::behavior::Event::Talk||event==mod_actor::behavior::Event::Hit)guest_call(c,dispatch->function,{handle,userdata,argument});
        else guest_call(c,dispatch->function,{handle,userdata});
    }catch(...){success=false;LOG("[guestmod:%s] actor %s callback failed; registration quarantined",identity.mod.c_str(),identity.id.c_str());}
    *c=saved;g_actor_draw_callback=prior_draw;g_actor_registry.finish_callback(dispatch->ticket,success);
    return success&&actor_binding(c,handle,identity.mod).has_value();
}
void behavior_sink(Cpu* c,uint32_t actor,mod_actor::BehaviorEvent event,const mod_actor::Hit* hit){
    using Event=mod_actor::BehaviorEvent;using Callback=mod_actor::behavior::Event;
    if(!c)return;
    struct PreserveCpu {Cpu* cpu;Cpu saved;~PreserveCpu(){*cpu=saved;}} preserve{c,*c};
    if(event==Event::Destroy){
        const uint32_t process=ld32(actor+4);
        for(auto it=g_actor_identities.begin();it!=g_actor_identities.end();)if(it->second.process_id==process){g_actor_registry.invalidate(it->first);it=g_actor_identities.erase(it);}else ++it;
        return;
    }
    mod_actor::Identity identity;if(!mod_actor::identify(c,actor,identity))return;
    uint32_t handle=mod_actor::behavior_handle(c,identity);
    if(!actor_binding(c,handle,identity.mod)){
        actor_sweep(c);
        const auto reason=event==Event::CreateRestored?mod_actor::behavior::Creation::Restored:mod_actor::behavior::Creation::New;
        const auto binding=g_actor_registry.issue(identity.mod,identity.token,identity.process_id,reason);if(!binding)return;
        handle=binding->handle;g_actor_identities.emplace(handle,identity);
        if(!mod_actor::set_behavior_handle(c,identity,handle)){g_actor_registry.invalidate(handle);g_actor_identities.erase(handle);return;}
        const uint32_t userdata=mod_actor::userdata_address(c,identity);
        if(binding->userdata_size&&!userdata){g_actor_registry.quarantine_registration(identity.mod,identity.token);return;}
        if(binding->initialize_userdata()&&binding->userdata_size)std::memset(mem::ptr(userdata),0,binding->userdata_size);
        if(!actor_dispatch(c,handle,Callback::Create,uint32_t(reason)))return;
    }
    if(event==Event::CreateNew||event==Event::CreateRestored)return;
    if(event==Event::Execute){actor_dispatch(c,handle,Callback::Execute);return;}
    if(event==Event::Draw){actor_dispatch(c,handle,Callback::Draw);return;}
    if(event==Event::TalkBegin||event==Event::TalkEnd||event==Event::TalkCancel){actor_dispatch(c,handle,Callback::Talk,event==Event::TalkBegin?0:event==Event::TalkEnd?1:2);return;}
    if(event!=Event::Hit||!hit)return;
    const uint32_t packet=mod_actor::hit_event_address(c,identity);if(!packet)return;
    uint32_t source_handle=0;
    if(hit->source_kind==2)for(const auto& [candidate,source]:g_actor_identities)if(source.mod==identity.mod&&source.process_id==hit->source_process_id&&actor_binding(c,candidate,identity.mod)){source_handle=candidate;break;}
    std::memset(mem::ptr(packet),0,64);st32(packet,64);st32(packet+4,1);st32(packet+8,hit->sequence);st32(packet+12,hit->attack_type);st32(packet+16,hit->attack_power);st32(packet+20,hit->source_kind);st32(packet+24,source_handle);
    const float values[]={hit->position.x,hit->position.y,hit->position.z,hit->reaction.x,hit->reaction.y,hit->reaction.z};
    for(unsigned i=0;i<6;++i){if(!std::isfinite(values[i]))return;stf32(packet+28+i*4,values[i]);}
    actor_dispatch(c,handle,Callback::Hit,packet);
}
void svc_actor_register(Cpu* c){
    auto& mod=owner(c);std::string id;const uint32_t pointer=c->r[4];c->r[3]=mod_string(mod,c->r[3],32,id)?1:0;
    if(!c->r[3]||!story_mail::valid_id(id)||!setting_buffer(mod,pointer,32)||pointer%4){c->r[3]=0;return;}
    const uint32_t token=mod_actor::declaration_token(mod.id,id);mod_actor::behavior::Table table;
    table.size=ld32(pointer);table.version=ld32(pointer+4);table.userdata_size=ld32(pointer+8);table.create=ld32(pointer+12);table.execute=ld32(pointer+16);table.draw=ld32(pointer+20);table.talk=ld32(pointer+24);table.on_hit=ld32(pointer+28);
    c->r[3]=g_actor_registry.register_behavior(mod.id,token,table,actor_owned_function);
    if(c->r[3]&&table.on_hit&&!mod_actor::set_hit_notifications(mod.id,id,true)){g_actor_registry.quarantine_registration(mod.id,token);c->r[3]=0;}
}
void svc_actor_find(Cpu* c){
    auto& mod=owner(c);std::string id;const bool valid=mod_string(mod,c->r[3],32,id)&&story_mail::valid_id(id);c->r[3]=0;if(!valid)return;
    for(const auto& [handle,identity]:g_actor_identities)if(identity.mod==mod.id&&identity.id==id&&actor_binding(c,handle,mod.id)){c->r[3]=handle;return;}
}
void svc_actor_userdata(Cpu* c){auto& mod=owner(c);const uint32_t handle=c->r[3];const auto binding=actor_binding(c,handle,mod.id);c->r[3]=binding&&binding->userdata_size?mod_actor::userdata_address(c,g_actor_identities.at(handle)):0;}
void svc_actor_get(Cpu* c){
    auto& mod=owner(c);const uint32_t handle=c->r[3],out=c->r[4];c->r[3]=0;
    if(!actor_binding(c,handle,mod.id)||!actor_buffer(c,mod,out,80)||ld32(out)!=80||ld32(out+4)!=1)return;
    mod_actor::State state;if(!mod_actor::snapshot(c,g_actor_identities.at(handle),state))return;
    const float values[]={state.position.x,state.position.y,state.position.z,state.rotation.x,state.rotation.y,state.rotation.z,state.collision_radius,state.collision_height};
    for(unsigned i=0;i<8;++i)stf32(out+8+i*4,values[i]);
    st32(out+40,(state.collision_enabled?1u:0u)|(state.pushable?2u:0u)|(state.scene_owned?4u:0u));std::memset(mem::ptr(out+44),0,36);std::memcpy(mem::ptr(out+44),state.animation.data(),std::min(size_t(32),state.animation.size()));c->r[3]=1;
}
std::optional<mod_actor::Identity> actor_mutable(Cpu* c){auto& mod=owner(c);const uint32_t handle=c->r[3];if(g_actor_draw_callback||!actor_binding(c,handle,mod.id))return {};return g_actor_identities.at(handle);}
void svc_actor_set_position(Cpu* c){const auto id=actor_mutable(c);c->r[3]=id&&mod_actor::set_position(c,*id,{float(c->f[1].ps0),float(c->f[2].ps0),float(c->f[3].ps0)});}
void svc_actor_set_rotation(Cpu* c){const auto id=actor_mutable(c);c->r[3]=id&&mod_actor::set_rotation(c,*id,{float(c->f[1].ps0),float(c->f[2].ps0),float(c->f[3].ps0)});}
void svc_actor_set_collision(Cpu* c){const uint32_t flags=c->r[4];const auto id=actor_mutable(c);c->r[3]=id&&!(flags&~3u)&&mod_actor::set_collision(c,*id,float(c->f[1].ps0),float(c->f[2].ps0),flags&1,flags&2);}
void svc_actor_set_animation(Cpu* c){
    auto& mod=owner(c);const auto id=actor_mutable(c);std::string clip;const float blend=float(c->f[1].ps0);const uint32_t loop=c->r[5];
    c->r[3]=id&&loop<=1&&std::isfinite(blend)&&blend>=0&&blend<=2&&mod_string(mod,c->r[4],32,clip)&&story_mail::valid_id(clip)&&mod_actor::set_animation(c,*id,clip,blend*30,loop);
}
void svc_actor_walk(Cpu* c){
    auto& mod=owner(c);const auto id=actor_mutable(c);const uint32_t points=c->r[4],count=c->r[5],loop=c->r[6];const float speed=float(c->f[1].ps0);c->r[3]=0;
    if(!id||count<2||count>16||loop>1||!actor_buffer(c,mod,points,count*12))return;
    std::vector<story_notebook::WorldPoint> path;for(unsigned i=0;i<count;++i)path.push_back({u32_as_f32(ld32(points+i*12)),u32_as_f32(ld32(points+i*12+4)),u32_as_f32(ld32(points+i*12+8))});c->r[3]=mod_actor::start_path(c,*id,path,speed,loop);
}
void svc_actor_walk_quest(Cpu* c){
    auto& mod=owner(c);const auto id=actor_mutable(c);std::string quest;const float speed=float(c->f[1].ps0);float x=0,z=0;mod_actor::State state;
    c->r[3]=id&&mod_string(mod,c->r[4],32,quest)&&story_mail::valid_id(quest)&&story_notebook::place_position(mod.id,quest,x,z)&&mod_actor::snapshot(c,*id,state)&&std::hypot(double(x)-state.position.x,double(z)-state.position.z)<=10000&&mod_actor::start_path(c,*id,{state.position,{x,state.position.y,z}},speed,false);
}
void svc_actor_stop(Cpu* c){const auto id=actor_mutable(c);c->r[3]=id&&mod_actor::stop_path(c,*id);}
void svc_scene_music_register(Cpu* c){
    auto& mod=owner(c);const uint32_t callback=c->r[3];c->r[3]=0;if(!callback||!actor_owned_function(mod.id,callback)||g_actor_registry.in_flight())return;
    const std::string owner_id=mod.id;
    c->r[3]=story_scenes::register_music_hook(owner_id,[owner_id,callback](Cpu* cpu,const std::string& cue,story_scenes::MusicPhase phase,unsigned fade,story_scenes::StopReason reason){
        Loaded* module=nullptr;for(auto& loaded:g_loaded)if(loaded.id==owner_id)module=&loaded;
        if(!cpu||!module||!actor_owned_function(owner_id,callback)||!story_mail::valid_id(cue))return false;
        const uint32_t text=module->heap->allocate(uint32_t(cue.size()+1));if(!text)return false;
        std::memcpy(mem::ptr(text),cue.c_str(),cue.size()+1);const Cpu saved=*cpu;bool success=false;
        try{success=guest_call(cpu,callback,{text,uint32_t(phase),fade,uint32_t(reason)})!=0;}catch(...){LOG("[guestmod:%s] scene music callback failed",owner_id.c_str());}
        *cpu=saved;module->heap->release(text);return success;
    });
}

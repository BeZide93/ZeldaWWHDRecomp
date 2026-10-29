// SPDX-License-Identifier: MPL-2.0
#include "mods/camera_distance.h"
#include "mods/enhanced_touch.h"
#include "mods/play_context.h"
#include <cassert>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <sys/mman.h>
namespace hostui {
bool get(const char*,std::string&) {return false;}
void set(const char*,const std::string&) {}
void post(std::function<void()> f) {f();}
}
namespace mods {
playctx::PlayContext context;
void play_context_want(bool) {}
playctx::PlayContext play_context() {return context;}
}
namespace mem { uint32_t host_alloc(uint32_t,uint32_t) {return 0x18000;} }
static float wall=10000, drawn=0;
uint32_t guest_call(Cpu* c,uint32_t,std::initializer_list<uint32_t> args) {
    const uint32_t end=*(args.begin()+2);
    c->r[3]=0; // collision helper must not corrupt the draw's CPU arguments
    return float(ldf32(end+8))>wall;
}
static void draw(Cpu* c) {assert(c->r[3]==0x10000);drawn=float(ldf32(c->r[3]+0xE4));}
int main() {
    auto ptr=mmap(ppc_ptr(0x10000),0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(ptr==ppc_ptr(0x10000));
    Cpu c{};c.r[3]=0x10000;
    stf32(0x100E4,100);
    mods::context.flags=mods::playctx::kInPlay;
    enhanced::distance_pinch.set(1);
    enhanced::set_camera_distance(2);
    auto eye=enhanced::draw_camera_distance(&c,draw);
    assert(drawn==200 && eye[2]==200 && ldf32(0x100E4)==100 && c.r[3]==0x10000);
    wall=150;enhanced::draw_camera_distance(&c,draw);
    assert(drawn>140 && drawn<150 && ldf32(0x100E4)==100);
    enhanced::set_camera_distance(.5);enhanced::draw_camera_distance(&c,draw);assert(drawn==50);
    mods::context.flags=mods::playctx::kEvent;
    enhanced::draw_camera_distance(&c,draw);assert(drawn==100);
    mods::context.flags=mods::playctx::kInPlay;mods::context.zoom_kind=1;
    enhanced::draw_camera_distance(&c,draw);assert(drawn==100);
    mods::context.zoom_kind=0;
    try {enhanced::draw_camera_distance(&c,[](Cpu*){throw std::runtime_error("draw");});assert(false);} catch(const std::runtime_error&) {}
    assert(ldf32(0x100E4)==100);
    enhanced::set_camera_distance(1);assert(enhanced::distance_gesture(0,100));enhanced::distance_gesture(1,200);
    assert(enhanced::camera_distance()==.5f);
    enhanced::distance_gesture(2,25);assert(enhanced::camera_distance()==2);
    enhanced::distance_pinch.set(0);assert(!enhanced::distance_gesture(0,100));
    enhanced::draw_camera_distance(&c,draw);assert(drawn==100);
    munmap(ptr,0x10000);
}

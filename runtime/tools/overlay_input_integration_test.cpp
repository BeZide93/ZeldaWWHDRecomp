// Actual SDL router + overlay.cpp input API; no game, renderer, audio or state files.
#include <SDL3/SDL.h>
#include "input.h"
#include "input_map.h"
#include "gfx/renderer.h"
#include "overlay/overlay.h"
#include "platform/input_sdl.h"
#include "platform/keycodes.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

static std::vector<std::string> logs;
static int saves=0,loads=0,promptKeys=0;
static bool prompt=false;
void log_msg(const char* format,...) {
 char line[2048];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);
 logs.emplace_back(line);
}
namespace audio { void flush(){} }
namespace ss { void request_save(int){++saves;} void request_load(int){++loads;} }
namespace render { uint64_t frame_count(){return 42;} Api active(){return Api::Vulkan;} }
namespace gx2 { void checkpoint_vulkan_caches(){} }
namespace gfxvk { bool graphics_hotkey(char,bool){return false;} }
namespace gfx { void display_plus_pressed(){} }
namespace mods { double game_time(){return 0;} void filter_pad(input::PadState&){} bool mouse_camera(){return false;} bool first_person_wheel(){return false;} void mouse_button(int,bool){} void mouse_add(float,float){} void mouse_wheel(float){} void fast_forward_reset(){} }
namespace interp { void set_mode(int){} uint64_t logic_steps(){return 0;} }
namespace timebase { uint64_t now(){return 0;} }
namespace true60 { uint64_t link_steps(){return 0;} bool state_loaded(){return false;} }
namespace true60_test { void set_origin_step(uint64_t){} void tick(double,bool){} }
namespace text_entry { bool active(){return prompt;} void text(const char*){} void preedit(const char*){} void key(int,bool,bool){++promptKeys;} }
namespace hostui { void graphics_changed(){} }
namespace screenshot { bool key_down(int){return false;} void poll_controller(const float*){} }

static void event_key(SDL_Window* window,SDL_Scancode key,bool down,SDL_Keymod mods=SDL_KMOD_NONE,bool repeat=false) {
 SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.windowID=SDL_GetWindowID(window);
 e.key.timestamp=123456789;e.key.scancode=key;e.key.mod=mods;e.key.repeat=repeat;
 input::handle_event(e);
}
int main(){
 SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS));
 auto* tv=SDL_CreateWindow("TV",32,32,SDL_WINDOW_HIDDEN);
 auto* drc=SDL_CreateWindow("GamePad",32,32,SDL_WINDOW_HIDDEN);
 auto* auxiliary=SDL_CreateWindow("Controls",32,32,SDL_WINDOW_HIDDEN);
 assert(tv&&drc&&auxiliary);input::set_prompt_window(tv);
#ifndef MENU112_BASELINE
 input::set_gamepad_window(drc);
#endif
 auto reset=[&](){overlay::set_open(false);event_key(tv,SDL_SCANCODE_F1,false);SDL_Delay(160);logs.clear();};
 auto f1=[&](SDL_Window* w,bool down){event_key(w,SDL_SCANCODE_F1,down);};
 auto focus=[&](){SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;e.window.windowID=SDL_GetWindowID(tv);e.window.timestamp=987654321;input::handle_event(e);
  e.type=SDL_EVENT_WINDOW_FOCUS_GAINED;e.window.windowID=SDL_GetWindowID(drc);input::handle_event(e);};
 auto result=[](const char* name,bool expected){printf("%s: %s\n",name,overlay::is_open()?"open":"closed");assert(overlay::is_open()==expected);};
#ifdef MENU112_BASELINE
 constexpr bool duplicateExpected=false;
#else
 constexpr bool duplicateExpected=true;
#endif
 reset();f1(tv,true);focus();result("focus after F1",true);
#ifndef MENU112_BASELINE
 auto logged=[](const std::string& needle){for(const auto& line:logs)if(line.find(needle)!=std::string::npos)return true;return false;};
 assert(logged("focus lost window TV id "+std::to_string(SDL_GetWindowID(tv))+" timestamp 987654321"));
 assert(logged("focus gained window GamePad id "+std::to_string(SDL_GetWindowID(drc))+" timestamp 987654321"));
#endif
 reset();f1(tv,true);f1(tv,true);result("duplicate F1 down on TV",duplicateExpected);
 reset();f1(tv,true);f1(tv,false);focus();f1(tv,true);result("F1 down/up/down on TV across focus",duplicateExpected);
 reset();f1(tv,true);focus();f1(drc,true);result("duplicate F1 down on GamePad",true);
 reset();f1(tv,true);f1(tv,false);focus();f1(drc,true);result("F1 down/up/down ending on GamePad",true);
#ifndef MENU112_BASELINE
 reset();f1(tv,true);focus();f1(drc,false);SDL_Delay(160);f1(tv,true);result("GamePad release then deliberate TV F1",false);
#endif
 reset();float pad[input_map::kPadCount]={};pad[input_map::kPadOptions]=1;
 overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());SDL_Delay(510);
 overlay::poll_controller_shortcuts(pad);assert(overlay::is_open());focus();pad[input_map::kPadOptions]=0;
 overlay::poll_controller_shortcuts(pad);result("View hold/release across focus",true);
#ifndef MENU112_BASELINE
 assert(logs.back().find("focus gained")!=std::string::npos);
 bool viewLogged=false;for(const auto& line:logs)viewLogged|=line.find("opened (View")!=std::string::npos;assert(viewLogged);
#endif
 // Releasing View does not close; the next press still closes once and stays latched.
 pad[input_map::kPadOptions]=1;overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
 overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
#ifndef MENU112_BASELINE
 assert(logs.back().find("closed (View")!=std::string::npos);
#endif
 pad[input_map::kPadOptions]=0;overlay::poll_controller_shortcuts(pad);
 reset();saves=loads=0;
 for(int i=0;i<5;++i)event_key(drc,SDL_Scancode(SDL_SCANCODE_F1+i),true,SDL_KMOD_SHIFT);
 for(int i=1;i<5;++i)event_key(drc,SDL_Scancode(SDL_SCANCODE_F1+i),true);
 printf("GamePad save/load requests: %d/%d\n",saves,loads);
#ifdef MENU112_BASELINE
 assert(saves==0&&loads==0);
#else
 assert(saves==5&&loads==4);
#endif
 const int beforeSaves=saves,beforeLoads=loads;prompt=true;
 event_key(drc,SDL_SCANCODE_F1,true,SDL_KMOD_SHIFT);event_key(drc,SDL_SCANCODE_F2,true);
 assert(saves==beforeSaves&&loads==beforeLoads&&promptKeys==2);prompt=false;
 reset();prompt=true;f1(tv,true);assert(overlay::is_open());prompt=false; // desktop F1 still opens settings over a text prompt
#ifndef MENU112_BASELINE
 reset();event_key(auxiliary,SDL_SCANCODE_F2,true);
 event_key(drc,SDL_SCANCODE_F2,true,SDL_KMOD_NONE,true);
 assert(saves==beforeSaves&&loads==beforeLoads); // auxiliary windows and repeats do not load
#endif
#ifndef MENU112_BASELINE
 // Real transitions emit one line, with the original SDL key provenance.
 reset();f1(tv,true);assert(overlay::is_open());
 auto matches=[](const std::string& needle){int n=0;for(const auto& line:logs)n+=line.find(needle)!=std::string::npos;return n;};
 assert(matches("[overlay] opened (F1 key, window TV id "+std::to_string(SDL_GetWindowID(tv))+", timestamp 123456789, frame 42)")==1);
 event_key(tv,SDL_SCANCODE_F1,true,SDL_KMOD_NONE,true);
 f1(tv,true);assert(matches("[overlay] opened (")==1&&matches("[overlay] closed (")==0);
 event_key(drc,SDL_SCANCODE_ESCAPE,true);assert(!overlay::is_open());
 assert(matches("[overlay] closed (Esc key, window GamePad id "+std::to_string(SDL_GetWindowID(drc))+", timestamp 123456789, frame 42)")==1);
 f1(tv,false);assert(matches("key F1 up window TV")==1);
 assert(matches("key F1 down window TV")>=2&&matches("key Escape down window GamePad")==1);
 event_key(drc,SDL_SCANCODE_ESCAPE,false);assert(matches("key Escape up window GamePad")==1);
 reset();f1(auxiliary,true);assert(!overlay::is_open()); // auxiliary windows do not toggle
 // Focus releases game keys while keeping an already open menu open.
 event_key(tv,SDL_SCANCODE_K,true);bool held[256];input::held_keys(held);assert(held[kVK_ANSI_K]);
 focus();input::held_keys(held);assert(!held[kVK_ANSI_K]);
 overlay::set_open(true);focus();assert(overlay::is_open());
 // Home retains press/release behavior and reports its reason.
 overlay::set_open(false);pad[input_map::kPadHome]=1;overlay::poll_controller_shortcuts(pad);assert(overlay::is_open());
 assert(logs.back().find("opened (Home")!=std::string::npos);
 overlay::poll_controller_shortcuts(pad);assert(overlay::is_open());pad[input_map::kPadHome]=0;overlay::poll_controller_shortcuts(pad);
 pad[input_map::kPadHome]=1;overlay::poll_controller_shortcuts(pad);assert(!overlay::is_open());
 assert(logs.back().find("closed (Home")!=std::string::npos);
 puts("real overlay transitions, key/focus trace, Home, text prompt and auxiliary-window checks passed");
#endif
 SDL_DestroyWindow(auxiliary);SDL_DestroyWindow(drc);SDL_DestroyWindow(tv);SDL_Quit();
}

// SPDX-License-Identifier: MPL-2.0
// Parts of a running mod demo (story_demo.h) that act on the game while the demo plays:
//  - light: the demo's time and weather changes, played by a LIGHT staff in the demo's event (the
//    game's own dKy_instant_timechg / dKy_instant_rainchg), held while the demo runs and restored
//    at its end, skip or state load.
// Their state lives in the demo's guest record (story_demo.cpp gives each part an area), so a state
// loaded mid-demo is released from guest memory. Both do nothing unless the running demo uses them.
#pragma once
#include "demo_scene.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>

struct Cpu;
namespace story_demo_parts {
using Call = uint32_t (*)(Cpu*, uint32_t, std::initializer_list<uint32_t>);  // a game call that keeps the CPU state
struct Context {
    uint32_t record = 0;   // the part's area in the demo's guest record
    uint32_t scratch = 0;  // 64 bytes of guest scratch
    Call call = nullptr;
    Call raw = nullptr;    // the same call without restoring the CPU (for f1 results)
};

namespace light {
inline constexpr uint32_t kRecordBytes = 0x80;
// Saves the time and weather the demo may change (at the demo's start, before its event runs).
void begin(Cpu*, const Context&, const demo_scene::Demo&);
// Each demo frame: the LIGHT staff's next cut starts at its frame (its start flag in the event
// manager's flags); a weather change is held from then on.
void update(Cpu*, const Context&, const demo_scene::Demo&, uint32_t frame, uint32_t event_manager);
// The demo ends: the time and weather before it come back unless the demo keeps them.
void release(Cpu*, const Context&);
// dKy_Execute (the environment's execute) is replaced only when a declared demo changes the weather;
// it is the game's own unless a demo holds the weather. On the Great Sea the held weather replaces
// the dice weather for that time; everything else the execute does still runs.
void install(const Context&, void (*original)(Cpu*, uint32_t), const std::function<std::string(uint32_t)>& replaced_by,
             const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool holding();
}  // namespace light
}  // namespace story_demo_parts

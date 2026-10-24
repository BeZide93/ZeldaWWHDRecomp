// The fog the HD shadow pass copies into its environment uniforms is pinned to the scene's normal fog.
//
// The game keeps one "current fog" object (found through the view's fog list, 3E9B9280 on the Outset
// cliff). Two cores draw into it at once: the main thread's sea and background draws (daSea_setUniforms
// 0246C7C8, daBg_Draw 02079D08, drawWave 02574D38) switch it to the sea fog with GxFog_sea_set
// (0255F8A0, colour from the environment light +0xB94) and back with GxFog_set (0255F84C, colour from
// +0xB8C), while WorkerMgr/Worker2() paints the layers: its line packets set the normal fog again
// (lineSetMaterial1 025EC090) and its shadow pass (hdLayerShadow drawShadowPass 02736C10 ->
// 02737D54 -> 027FDA54) copies the current fog colour, start and end into the environment uniforms
// that update_ubo then uploads (the scene blocks at 3B5B2E00 / 4581D200, +0x190). When the copy fell
// into the main thread's short sea-fog window, the sea fog colour got into the scene constants: the
// distant sea and grass changed from run to run (#178).
//
// The Wii U has the same unsynchronised sharing; its fixed timing just makes the outcome stable, and in
// the worker's own program order the fog it reads is the normal fog its line packets set right before
// (the shadow pass draws no sea). So the copy reads the normal fog, computed the way GxFog_set computes
// it (0255F84C -> 0255F668: colour = environment light +0xB8C..B8F through GXColor_to_f32 02556DE4,
// start = +0xFD0, end = +0xFD4). Those environment fields are written by the environment logic only,
// never while the frame is painted, so the result no longer depends on the other core.
//
// Safe: only this read changes. The site redirects the fog object register r31 at 027FDF78, inside
// 027FDA54 after its own lookup and its "fog enabled" checks, to a private copy that holds the
// normal-fog fields 027FDA54 reads from there (+0xF4, +0x104, +0x114..+0x120); r31 is restored by the
// function's epilogue. Nothing is written to the shared fog object, other callers of 027FDA54 (the
// main thread's model draws, including those under the sea fog) and the sea / background fog keep
// their behaviour, and the copy lives in host scratch memory (not part of save states).
// WWHD_FOG_PIN=0 turns the pin off (comparisons).
#include <cstdlib>
#include <cstring>

#include "guest_addr.h"
#include "runtime.h"

namespace interp_fx { bool in_shadow_pass(); }

namespace {
bool pin_on() {
    static const bool on = [] { const char* e = getenv("WWHD_FOG_PIN"); return !e || atoi(e) != 0; }();
    return on;
}
// GXColor_to_f32 (02556DE4): each channel (float)byte / 255.0f, the divisor read from the game's constant
float channel(uint32_t byte, float divisor) { return (float)((double)(float)byte / (double)divisor); }
uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
}  // namespace

extern "C" void site_027FDF78(Cpu* c) {
    if (!pin_on() || !interp_fx::in_shadow_pass()) return;
    if (!ld32(GD(0x104773B4))) return;  // environment light not set up yet (dKy_getEnvlight 02555D0C)
    const uint32_t env = GD(0x10475A68);
    thread_local uint32_t copy = 0;  // per thread: the TV's and the GamePad's layers paint on their own threads
    if (!copy) copy = mem::host_alloc(0x130);
    float divisor;
    { const uint32_t w = ld32(GD(0x1004F104)); memcpy(&divisor, &w, 4); }
    st32(copy + 0xF4, ld32(env + 0xFD0));
    st32(copy + 0x104, ld32(env + 0xFD4));
    for (uint32_t i = 0; i < 4; i++) st32(copy + 0x114 + 4 * i, bits(channel(ld8(env + 0xB8C + i), divisor)));
    c->r[31] = copy;
}

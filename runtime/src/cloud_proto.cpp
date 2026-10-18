// Cloud research prototype (off by default; WWHD_CLOUDS_PROTO=sdf|volume|hide).
//
// The game's sky clouds (vrkumo) are drawn by drawVrkumo (02575B6C): 3 layers x up to 100 camera-facing
// textured quads on a sphere at the far plane, one draw each, positions from g_env_light.mpVrkumoPacket
// (+0xA94, VRKUMO_EFF[100] at +0xA4, 0x2C each). This hook reads that packet, the camera, the view and
// the environment palette READ-ONLY on the game thread, keeps a host-side snapshot and brackets the
// game's own draws with two render-queue ops (OP_CLOUDS_BEGIN / OP_CLOUDS_END). The renderer skips
// the game's cards between the two and draws its own clouds instead (gfx/metal_clouds.mm). Nothing
// is written to guest memory.
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "cloud_proto.h"
#include "guest_addr.h"
#include "gx2/gx2_cmd.h"
#include "runtime.h"

extern "C" void f_02575B6C_orig(Cpu* c);  // drawVrkumo

namespace cloud_proto {

uint32_t g_active_seq = 0;
bool g_drawn = false;
void (*g_on_end)() = nullptr;
static Snapshot g_ring[kRing];
static std::atomic<uint32_t> g_seq{0};

int mode() {
    static const int m = [] {
        const char* e = getenv("WWHD_CLOUDS_PROTO");
        if (!e || !*e || !strcmp(e, "0") || !strcmp(e, "off")) return kOff;
        if (!strcmp(e, "volume")) return kVolume;
        if (!strcmp(e, "hide")) return kHide;
        if (!strcmp(e, "mark")) return kMark;
        if (!strcmp(e, "combo")) return kCombo;
        return kSdf;
    }();
    return m;
}

const Snapshot* snapshot(uint32_t seq) { return &g_ring[seq % kRing]; }

static float ldf(uint32_t ea) { return u32_as_f32(ld32(ea)); }
static void vec3(uint32_t ea, float* o) { o[0] = ldf(ea), o[1] = ldf(ea + 4), o[2] = ldf(ea + 8); }
static void rgb(uint32_t ea, float* o) { o[0] = ld8(ea) / 255.0f, o[1] = ld8(ea + 1) / 255.0f, o[2] = ld8(ea + 2) / 255.0f; }

static bool take(Snapshot& s) {
    static const uint32_t kEnvLight = GD(0x10475A68);  // g_env_light (dKy_getEnvlight 02555D0C)
    static const uint32_t kPlay = GD(0x1046F0B0);      // g_dComIfG_gameInfo.play (025200D4)
    uint32_t packet = ld32(kEnvLight + 0xA94);
    uint32_t camera = ld32(kPlay + 0x5AF8), view = ld32(kPlay + 0x5FA4);
    if (!packet || !camera || !view) return false;
    vec3(camera + 0xDC, s.eye);
    vec3(camera + 0xE8, s.center);
    s.bank = (float)(int16_t)ld16(camera + 0x100) * (3.14159265f / 32768.0f);
    s.fovy = ldf(view + 0xD4);
    for (int i = 0; i < 12; i++) s.viewRot[i] = ldf(view + 0x1E4 + 4 * i);
    s.sunVisible = 0;
    if (uint32_t sun = ld32(kEnvLight + 0xA34)) {
        vec3(sun + 0x98, s.sunPos);
        s.sunVisible = 1;
        s.sunAlpha = ldf(sun + 0xCC);
    }
    vec3(kEnvLight + 0xB44, s.moonPos);  // mMoonPos
    rgb(kEnvLight + 0xB98, s.cloudColor);
    rgb(kEnvLight + 0xB9C, s.centerColor);
    rgb(kEnvLight + 0xB8C, s.fogColor);
    s.cloudScale = ldf(kEnvLight + 0x10DC);
    s.centerScale = ldf(kEnvLight + 0x10E0);
    s.strength = ldf(kEnvLight + 0xA90);
    s.count = (int32_t)ld32(kEnvLight + 0xA90 - 4);  // mVrkumoCount (+0xA8C)
    s.curTime = ldf(kEnvLight + 0x1020);
    s.phase = ldf(packet + 0x11D4);
    static const uint32_t kConst[8] = {0x1004FCD8, 0x1004FCDC, 0x1004FCF8, 0x1004FCFC,
                                       0x1004FD00, 0x1004FD04, 0x1004FC98, 0x1004FCC8};
    for (int i = 0; i < 8; i++) s.k[i] = ldf(GD(kConst[i]));
    s.n = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t e = packet + 0xA4 + 0x2C * i;
        float alpha = ldf(e + 0x20);
        if (!(alpha > 0.0001f)) continue;
        Cloud& c = s.c[s.n++];
        vec3(e + 4, c.pos);
        c.height = ldf(e + 0x1C);
        c.alpha = alpha;
        c.falloff = ldf(e + 0x24);
        c.index = i;
    }
    return true;
}

}  // namespace cloud_proto

using namespace cloud_proto;

extern "C" void hook_02575B6C(Cpu* c) {
    if (!mode()) { f_02575B6C_orig(c); return; }
    uint32_t seq = g_seq.load(std::memory_order_relaxed) + 1;
    if (!take(g_ring[seq % kRing])) { f_02575B6C_orig(c); return; }
    g_seq.store(seq, std::memory_order_release);
    gx2::emit(gx2::OP_CLOUDS_BEGIN, {seq});
    f_02575B6C_orig(c);
    gx2::emit(gx2::OP_CLOUDS_END, {seq});
}

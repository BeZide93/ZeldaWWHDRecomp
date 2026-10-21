// SPDX-License-Identifier: MPL-2.0
// Safety net: a J3D draw bucket whose packet list loops on itself is cut before the game walks it
// (j3d_draw_guard.h). J3DDrawBuffer::drawHead (027F10CC), drawTail (027F1174) and frameInit (027F093C)
// walk every bucket until a packet's next link is 0. A packet entered twice before frameInit (an actor
// drawn twice in one frame) closes a loop, and the walk never ends: the game thread draws the same
// packets until its display list is full and keeps going (2026-10-21: the editor's scene preview hang;
// story_demo.cpp creates its actors in Link's layer now, which removed that cause). Here the link that
// closes the loop is set to 0, so each packet is drawn once and the frame finishes. A list without a
// loop is left as it is; a loop is said once in the log with the buffer, bucket and packet.
#include "j3d_draw_guard.h"

#include <atomic>
#include <cstdint>

#include "runtime.h"

extern "C" void f_027F10CC_orig(Cpu* c);  // J3DDrawBuffer::drawHead(this, context)
extern "C" void f_027F1174_orig(Cpu* c);  // J3DDrawBuffer::drawTail(this, context)
extern "C" void f_027F093C_orig(Cpu* c);  // J3DDrawBuffer::frameInit(this)

namespace {
// J3DDrawBuffer: +0 bucket count, +4 the buckets (one packet pointer each); J3DPacket +0x10: next
constexpr uint32_t kBucketCount = 0x0, kBuckets = 0x4, kNext = 0x10;
constexpr uint32_t kMaxBuckets = 0x10000;  // more is not a draw buffer: left alone
std::atomic<unsigned> g_cuts{0};

bool guest_word(uint32_t address) { return address >= mem::kMem2Start && address <= mem::kMem2End - 4 && !(address & 3); }

void cut_loops(uint32_t buffer, const char* walk) {
    if (!guest_word(buffer) || !guest_word(buffer + kBuckets)) return;
    const uint32_t count = ld32(buffer + kBucketCount), buckets = ld32(buffer + kBuckets);
    if (!count || count > kMaxBuckets || !guest_word(buckets) || !guest_word(buckets + (count - 1) * 4)) return;
    // a link outside guest memory ends the walk here (the game's own walk would fault on it anyway)
    auto next = [](uint32_t packet) { return guest_word(packet + kNext) ? ld32(packet + kNext) : 0u; };
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t head = ld32(buckets + i * 4);
        const uint32_t last = j3d_draw_guard::loop_end(head, next);
        if (!last) continue;
        const uint32_t first = ld32(last + kNext);
        st32(last + kNext, 0);
        const unsigned n = ++g_cuts;
        if (n <= 4 || (n & (n - 1)) == 0)
            LOG("[j3d] draw buffer %08X bucket %u looped on itself (packet %08X entered twice; the loop closes at %08X): "
                "cut before %s so the frame goes on (%u so far)",
                buffer, i, first, last, walk, n);
    }
}
}  // namespace

extern "C" void hook_027F10CC(Cpu* c) {
    cut_loops(c->r[3], "drawHead");
    f_027F10CC_orig(c);
}
extern "C" void hook_027F1174(Cpu* c) {
    cut_loops(c->r[3], "drawTail");
    f_027F1174_orig(c);
}
extern "C" void hook_027F093C(Cpu* c) {
    cut_loops(c->r[3], "frameInit");
    f_027F093C_orig(c);
}

// Diagnostic: WWHD_DTRACE=path writes one line per game swap (game thread) with a digest of the
// game state: g_Counter.mTimer (ctr, counts gameplay frames) and mCounter0 (tick, counts every
// game frame including loading screens), cM_rnd's seeds, the environment clock and the sky-cloud
// packet. Two runs whose loads took the same number of frames write identical files; the game
// itself reads mCounter0 (e.g. the wind-line spawn gate in rainWindMove), so runs whose boot took a
// different number of frames diverge from there, as on the console.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "guest_addr.h"
#include "runtime.h"

namespace dtrace {
void on_swap(uint64_t swap, uint64_t steps) {
    static FILE* f = [] {
        const char* p = getenv("WWHD_DTRACE");
        return p ? fopen(p, "w") : nullptr;
    }();
    if (!f) return;
    const uint32_t env = GD(0x10475A68);
    const uint32_t packet = ld32(env + 0xA94);
    uint64_t h = 1469598103934665603ull;
    if (packet)
        for (uint32_t a = packet + 0xA4; a < packet + 0xA4 + 0x2C * 100; a += 4) h = (h ^ ld32(a)) * 1099511628211ull;
    fprintf(f, "%llu %llu ctr %08X tick %08X rnd %08X %08X %08X time %08X clouds %016llx\n", (unsigned long long)swap,
            (unsigned long long)steps, ld32(GD(0x101FF560)), ld32(GD(0x101FF558)), ld32(GD(0x101FF9D4)), ld32(GD(0x101FF9D4) + 4),
            ld32(GD(0x101FF9D4) + 8), ld32(env + 0x1020), (unsigned long long)h);
    fflush(f);
}
}  // namespace dtrace

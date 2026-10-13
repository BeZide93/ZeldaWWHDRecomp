// A read-only synthetic guest heap: the reader must never write to the game or call guest code.
#include "gfx/display_modes.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cassert>
#include <sys/mman.h>
#include <unistd.h>

void log_msg(const char*, ...) {}

extern "C" {
// Deliberately shift canonical data, as a regional build does.
const GuestStep g_guest_data_steps[] = {{0x10000000, 0x1000}};
const unsigned g_guest_data_step_count = 1;
const uint32_t g_guest_data_lo = 0x10000000, g_guest_data_hi = 0x11000000;
const char g_guest_build_name[] = "test regional map";
}
namespace {
bool needed = false;
gfx::GamePadUiState observed;
void map(uint32_t at, size_t size) {
    void* p = mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    assert(p == mem::ptr(at));
}
void protect(uint32_t at, size_t size, int flags) { assert(mprotect(mem::ptr(at), size, flags) == 0); }
}
namespace gfx {
bool display_gamepad_state_needed() { return needed; }
void display_gamepad_ui(GamePadUiState state) { observed = state; }
}
int main() {
    using namespace gfx;
    display_read_gamepad_ui(); // no mapped memory: inactive modes must not read it
    needed = true;
    const uint32_t globals = 0x101F0000, heap = 0x11000000;
    const size_t region = 0x10000;
    map(globals, region); map(heap, region);
    const uint32_t scene = heap, input = heap + 0x1000;
    st32(GD(0x101F8344), scene); st32(GD(0x101F5088), input);
    st32(scene + 0x18, scene + 4); // state object is the embedded state interface
    const auto set = [&](uint32_t id, uint32_t routing, uint32_t mode) {
        protect(globals, region, PROT_READ | PROT_WRITE);
        protect(heap, region, PROT_READ | PROT_WRITE);
        st32(scene + 12, GD(id)); st32(GD(0x101F8348), routing); st32(input + 0x1d0, mode);
        protect(globals, region, PROT_READ); protect(heap, region, PROT_READ);
        display_read_gamepad_ui(); // writes would fault
    };
    set(0x1049EC00, 0, 1);
    assert(observed.available && !observed.menu && !observed.off_tv);
    set(0x1049ECB0, 2, 1);
    assert(observed.available && observed.menu);
    protect(heap, region, PROT_READ | PROT_WRITE);
    st32(scene + 0x17c, heap + 0x2000); st32(heap + 0x2054, 2); // Delivery Bag
    protect(heap, region, PROT_READ);
    set(0x1049EC00, 0, 1);
    assert(observed.menu); // read the bag lifecycle independently of scene transitions
    protect(heap, region, PROT_READ | PROT_WRITE);
    st32(heap + 0x2054, 3);
    protect(heap, region, PROT_READ);
    set(0x1049EC00, 0, 1);
    assert(!observed.menu);
    set(0x1049EC58, 4, 1); // cutscene-induced PauseDRC is not an interactive menu
    assert(observed.available && !observed.menu);
    set(0x1049ECB0, 1, 2);
    assert(observed.off_tv && !observed.menu);
    set(0x1049F498, 0, 1);
    assert(observed.switching);
    set(0x1049ECB0, 4, 0);
    assert(!observed.menu); // Pro-controller menus on TV
    protect(heap, region, PROT_READ | PROT_WRITE);
    st32(scene + 0x18, 0); st32(scene + 0x14, GD(0x1049EC84));
    protect(heap, region, PROT_READ);
    set(0x1049EC00, 0, 1);
    assert(observed.menu); // pending state, matching 02006478 fallback
    protect(globals, region, PROT_READ | PROT_WRITE);
    st32(GD(0x101F8344), 0);
    protect(globals, region, PROT_READ);
    display_read_gamepad_ui();
    assert(!observed.available && !observed.menu && !observed.off_tv && !observed.switching);
    assert(munmap(mem::ptr(globals), region) == 0);
    assert(munmap(mem::ptr(heap), region) == 0);
    needed = false;
    display_read_gamepad_ui(); // no new guest reads after leaving auto
}

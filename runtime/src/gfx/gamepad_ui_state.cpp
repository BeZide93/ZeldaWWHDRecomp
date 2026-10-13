// Read-only view of SCENE-ingame_ui and hdInputManager from the HD decomp.
// Called on the guest thread at GX2SwapScanBuffers, never from a GPU completion handler.
#include "display_modes.h"
#include "auto_pip.h"
#include "runtime.h"
#include "guest_addr.h"
#include <cstdlib>

namespace gfx {
namespace {
bool object(uint32_t p, uint32_t size) {
    return (p & 3) == 0 && p >= mem::kMem2Start && p <= mem::kMem2End - size;
}
// Scene descriptors are 0x2c bytes apart, but regional data maps may split that range.
uint32_t canonical_state(uint32_t p) {
    for (uint32_t state = 0x1049EBD4; state <= 0x1049F4F0; state += 0x2c)
        if (p == GD(state)) return state;
    return 0;
}
}
void display_read_gamepad_ui() {
    if (!display_gamepad_state_needed()) return;
    GamePadUiState state;
    {
        const uint32_t scene = ld32(GD(0x101F8344));
        const uint32_t input = ld32(GD(0x101F5088));
        if (object(scene, 0x24c) && object(input, 0x270)) {
            // hdStateMachine +8 is the current state object; its +8 is the descriptor.
            // With no current object, +4 is the pending descriptor (02006478).
            const uint32_t current = ld32(scene + 0x18);
            const uint32_t descriptor = object(current, 12) ? ld32(current + 8) : ld32(scene + 0x14);
            const uint32_t id = canonical_state(descriptor);
            const uint32_t controller = ld32(input + 0x1d0);
            state.available = id != 0 && controller <= 3;
            state.off_tv = controller == 2;
            state.switching = gamepad_switch_state(id);
            const uint32_t routing = ld32(GD(0x101F8348));
            // MENU-menu_main::openWindow (0268EC88) selects one of the three bag windows;
            // finishSaveWindow (02692A50) and requestOutAll (026942E0) restore sentinel 3.
            const uint32_t items = ld32(scene + 0x17c);
            const bool bag = object(items, 0x58) && ld32(items + 0x54) < 3;
            state.menu = gamepad_menu_state(id, routing, controller, bag);
            // Opt-in evidence for scripted menu/cutscene captures; no image measurements.
            static const bool trace = getenv("WWHD_TEST_GAMEPAD_UI_TRACE") != nullptr;
            if (trace) {
                static uint32_t previous = ~0u, previousRouting = ~0u, previousController = ~0u;
                static bool previousMenu = false;
                if (id != previous || routing != previousRouting || controller != previousController || state.menu != previousMenu) {
                    LOG("[display] GamePad UI: state=%08X routing=%u controller=%u menu=%d off-tv=%d switching=%d",
                        id, routing, controller, state.menu, state.off_tv, state.switching);
                    previous = id; previousRouting = routing; previousController = controller; previousMenu = state.menu;
                }
            }
        }
    }
    display_gamepad_ui(state);
}
} // namespace gfx

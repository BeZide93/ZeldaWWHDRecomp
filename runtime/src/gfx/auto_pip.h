// Host-only policy. The input describes the game's screen routing, never requests a game change.
#pragma once
#include <cstdint>
namespace gfx {
struct GamePadUiState {
    bool available = false;
    bool menu = false;
    bool off_tv = false;
    bool switching = false;
};
// Canonical USA state descriptors (translated with GD by the reader).
inline bool gamepad_menu_state(uint32_t state, uint32_t routing, uint32_t controller, bool bag_open = false) {
    // 2 routes the pause/menu overlay to DRC; 4 also occurs during ordinary cutscenes.
    if (controller != 1) return false;
    if (routing == 2 || bag_open) return true;
    switch (state) {
    case 0x1049EC84: case 0x1049ECB0: case 0x1049ECDC: // menu, including map/bags/collection
    case 0x1049ED08: case 0x1049ED34: // save prompt
    case 0x1049ED60: case 0x1049ED8C: // options
    case 0x1049EE10: case 0x1049EE3C: // song list
    case 0x1049EEEC: case 0x1049EF18: // warp map
    case 0x1049EF44: case 0x1049EF70: // photo/social prompts
    case 0x1049EF9C: case 0x1049EFC8:
    case 0x1049EFF4: case 0x1049F020:
    case 0x1049F04C: case 0x1049F078: case 0x1049F0A4:
    case 0x1049F0D0: case 0x1049F0FC:
    case 0x1049F1D8: case 0x1049F204: // photo lists
    case 0x1049F288: case 0x1049F2B4:
    case 0x1049F2E0: case 0x1049F30C: // Fishman hint
    case 0x1049F338: // password keyboard
        return true;
    default: return false;
    }
}
inline bool gamepad_switch_state(uint32_t state) {
    return state == 0x1049F498 || state == 0x1049F4C4 || state == 0x1049F4F0;
}
struct AutoPipDecision {
    double until = 0;
    void update(GamePadUiState state, double now) {
        if (!state.available || state.off_tv || state.switching) until = 0;
        else if (state.menu) until = now + 0.25;
    }
    bool shown(GamePadUiState state, bool pin, double now) const {
        return !state.off_tv && !state.switching && (pin || (state.available && (state.menu || now < until)));
    }
};
} // namespace gfx

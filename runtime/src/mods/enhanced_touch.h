// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <atomic>
#include <cstdlib>
#include <string>
#include "../overlay/hostui.h"
namespace enhanced {
// Independent settings: no changes to upstream's touch-options wire format.
struct Option {
    const char* key; int initial, maximum;
    std::atomic<int> value{-1};
    int get() {
        int v = value.load();
        if (v >= 0) return v;
        std::string s;
        v = hostui::get(key, s) ? atoi(s.c_str()) : initial;
        v = v < 0 ? 0 : v > maximum ? maximum : v;
        int unset = -1;
        return value.compare_exchange_strong(unset, v) ? v : unset;
    }
    void set(int v) {
        v = v < 0 ? 0 : v > maximum ? maximum : v;
        value = v;
        const std::string name(key);
        hostui::post([name, v] { hostui::set(name.c_str(), std::to_string(v)); });
    }
};
inline std::atomic<bool> sequence_active{false};
inline Option combat{"enhancedCombatButtons", 0, 15};
inline Option haptics{"enhancedTouchHaptics", 0, 1};
inline Option double_tap{"enhancedDoubleTap", 0, 1};
inline Option distance_pinch{"enhancedDistancePinch", 0, 1};
}

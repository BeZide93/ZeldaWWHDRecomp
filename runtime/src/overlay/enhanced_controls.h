// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "imgui.h"
#include "../platform/cpu_affinity.h"
#include "../mods/enhanced_touch.h"
#include "../mods/camera_distance.h"
namespace enhanced {
inline void cpu_rows() {
#ifdef __ANDROID__
    int mode = affinity::mode();
    if (ImGui::Combo("CPU core policy", &mode, "Automatic (upstream)\0Performance cores\0Prime renderer\0Prime game thread\0")) affinity::set_mode(mode);
    ImGui::TextWrapped("%s", affinity::status());
    ImGui::TextWrapped("Manual modes keep Android performance and thermal hints active. Prime modes pin the chosen thread to the fastest cluster and other frame threads to performance cores. Device permissions may restrict affinity.");
#endif
}
inline void touch_rows() {
#ifdef __ANDROID__
    ImGui::SeparatorText("Enhanced touch controls");
    bool h = haptics.get();
    if (ImGui::Checkbox("Touch button vibration", &h)) haptics.set(h);
    bool d = double_tap.get();
    if (ImGui::Checkbox("Double-tap right camera area to center", &d)) double_tap.set(d);
    bool zoom = distance_pinch.get();
    if (ImGui::Checkbox("Pinch changes camera distance", &zoom)) { distance_pinch.set(zoom); set_camera_distance(1); }
    if (zoom) {
        float value = camera_distance();
        if (ImGui::SliderFloat("Camera distance", &value, .5f, 2.f, "%.2fx")) set_camera_distance(value);
        if (ImGui::Button("Reset camera distance")) set_camera_distance(1);
        ImGui::TextWrapped("Overrides FOV pinch while enabled. The camera avoids walls when zooming out. Cutscenes and first-person cameras keep their original distance.");
    }
    int mask = combat.get();
    const char* names[] = {"Jump attack", "Vertical slash", "Spin attack", "Dodge / backflip"};
    for (int i = 0; i < 4; i++) {
        bool on = (mask & (1 << i)) != 0;
        if (ImGui::Checkbox(names[i], &on)) {
            mask = on ? mask | (1 << i) : mask & ~(1 << i); combat.set(mask);
        }
    }
    ImGui::TextWrapped("Combat buttons use normal timed inputs; equip a sword for attacks. Move and resize them in the touch layout editor. Dodge uses your stick direction, or backflips at rest.");
#endif
}
}

// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "imgui.h"
#include "../platform/frame_generation.h"
#include "../mods/enhanced_touch.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
namespace enhanced::hud {
enum Row { Fps, Interval, Average, Thermal, Gpu, Device, Detail, Graph, Settings, CpuUse, CpuTemp, GpuTemp, Count };
inline Option rows{"enhancedHudRows", (1 << Count)-1, (1 << Count)-1};
inline Option x{"enhancedHudX", 0, 100}, y{"enhancedHudY", 0, 100};
inline Option scale{"enhancedHudScale", 100, 200}, opacity{"enhancedHudOpacity", 55, 100};
inline bool editing = false;
inline bool show(Row row) { return (rows.get() & (1 << row)) != 0; }
inline void settings() {
    if (!ImGui::TreeNode("Customize performance HUD")) return;
    const char* names[] = {"FPS", "Frame interval", "Average and logic rate", "Temperatures / thermal state", "GPU load", "Device", "Detailed timings", "Frame-time graph", "Active settings", "Process CPU use", "CPU temperature", "GPU temperature"};
    int mask = rows.get();
    for (int i=0;i<Count;i++) {
        bool on = mask & (1 << i);
        if (ImGui::Checkbox(names[i], &on)) { mask = on ? mask | (1<<i) : mask & ~(1<<i); rows.set(mask); }
    }
    int px=x.get(), py=y.get(), size=scale.get(), alpha=opacity.get();
    if (ImGui::SliderInt("HUD horizontal position", &px, 0, 100, "%d%%")) x.set(px);
    if (ImGui::SliderInt("HUD vertical position", &py, 0, 100, "%d%%")) y.set(py);
    if (ImGui::SliderInt("HUD scale", &size, 50, 200, "%d%%")) scale.set(size);
    if (ImGui::SliderInt("HUD background", &alpha, 0, 100, "%d%%")) opacity.set(alpha);
    ImGui::Checkbox("Drag HUD while settings are open", &editing);
    if (ImGui::Button("Reset HUD layout")) { x.set(0); y.set(0); scale.set(100); opacity.set(55); rows.set((1<<Count)-1); }
    ImGui::TextWrapped("Position is relative to the available screen area and adapts to rotation. Optional device sensors may be unavailable. CPU use is summed across cores (100%% = one busy core).");
    ImGui::TreePop();
}
inline ImVec2 last_size{300, 200};
inline void begin(bool menu) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * std::max(50,scale.get()) / 100.f);
    auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(10 + std::max(0.f, display.x-last_size.x-20)*x.get()/100.f,
                                 10 + std::max(0.f, display.y-last_size.y-20)*y.get()/100.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(opacity.get()/100.f);
}
inline void controls(bool menu) {
    if (menu && editing) {
        ImGui::Button("Drag HUD", ImVec2(100, 30));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            auto d=ImGui::GetIO().DisplaySize, delta=ImGui::GetIO().MouseDelta;
            // Accumulate fractions; do not lose sub-percent touch movement.
            static float px=0, py=0;
            if (ImGui::IsItemActivated()) { px=x.get(); py=y.get(); }
            else if (!ImGui::IsMouseDragging(0,0)) { px=x.get(); py=y.get(); }
            // Values track the persisted options, plus fractional movement below one percent.
            if (std::abs(px-x.get())>1) px=x.get();
            if (std::abs(py-y.get())>1) py=y.get();
            px=std::clamp(px+delta.x*100/std::max(1.f,d.x-last_size.x-20),0.f,100.f);
            py=std::clamp(py+delta.y*100/std::max(1.f,d.y-last_size.y-20),0.f,100.f);
            x.value=int(px); y.value=int(py);
        }
        if (ImGui::IsItemDeactivated()) { x.set(x.get()); y.set(y.get()); }
    }
}
inline void end() { last_size=ImGui::GetWindowSize(); }
inline void cpu() {
    using clock = std::chrono::steady_clock;
    static auto last=clock::now(); static auto ticks=std::clock(); static double use=0;
    const auto now=clock::now(); const double seconds=std::chrono::duration<double>(now-last).count();
    if (seconds>=1) { auto next=std::clock(); use=100.*(next-ticks)/CLOCKS_PER_SEC/seconds; ticks=next; last=now; }
    ImGui::Text("Process CPU %.0f%% (100%% = one core)",use);
}
inline void telemetry(const std::string& text) {
    std::istringstream lines(text); std::string line;
    while (std::getline(lines,line)) {
        const Row row=line.starts_with("GPU busy") ? Gpu : line.find("cpu")!=std::string::npos ? CpuTemp :
                      line.find("gpu")!=std::string::npos ? GpuTemp : Thermal;
        if (show(row) && !line.empty()) ImGui::TextUnformatted(line.c_str());
    }
}
}

#include "editor_resident.h"

#include "imgui.h"

namespace editor_resident {

const std::array<Type, 3>& types() {
    static const std::array<Type, 3> list = {{
        {"uo2", "Uo2: the man who sits at Windfall's bar"},
        {"um1", "Um1: the man on Windfall's windmill"},
        {"ym1", "Ym1: a seated villager"},
    }};
    return list;
}

std::string label_of(const std::string& code) {
    for (const auto& t : types())
        if (code == t.code) return t.label;
    return code;
}

bool combo(const char* id, const std::string& current, std::string& chosen) {
    bool picked = false;
    const std::string shown = label_of(current);
    if (ImGui::BeginCombo(id, shown.c_str())) {
        for (const auto& t : types()) {
            const bool selected = current == t.code;
            if (ImGui::Selectable(t.label, selected) && !selected) chosen = t.code, picked = true;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return picked;
}

bool menu(const char* label, std::string& chosen) {
    bool picked = false;
    if (ImGui::BeginMenu(label)) {
        for (const auto& t : types())
            if (ImGui::MenuItem(t.label)) chosen = t.code, picked = true;
        ImGui::EndMenu();
    }
    return picked;
}

}  // namespace editor_resident

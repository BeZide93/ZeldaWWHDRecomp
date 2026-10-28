// The common settings list (game_settings.h).
#include "game_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>

#include "input_map.h"
#include "manager.h"
#include "mods.h"
#include "packages.h"
#include "../overlay/hostui.h"

namespace game_settings {
namespace {

std::mutex g_mu;
std::vector<Item>& registry() {
    static std::vector<Item> items;
    return items;
}
std::map<std::string, float> g_pending;  // id -> value posted, not yet applied (g_mu)

const char* const kTabOrder[] = {"camera", "controls", "hud", "mods"};

// a built-in mod's switch (mods/manager.cpp: the same entry the Mods tab switches)
Item builtin_toggle(const char* tab, const char* mod_id, const char* label, const char* help) {
    Item it;
    it.id = mod_id, it.tab = tab, it.label = label, it.help = help, it.kind = Kind::Toggle;
    const std::string id = mod_id;
    it.get = [id] {
        const auto* e = mods::manager::find(id);
        return e && e->enabled() ? 1.0f : 0.0f;
    };
    it.set = [id](float v) { mods::manager::set_enabled(id, v >= 0.5f); };
    return it;
}

std::string times(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2fx", v);
    return b;
}

}  // namespace

void set_camera_speed(float speed) {
    mods::set_camera_speed(speed);
    hostui::set("mod.direct-camera.speed", std::to_string(speed));
    mods::packages::remember_option("direct-camera.speed", speed);
}
void set_mouse_sensitivity(float degrees) {
    mods::set_mouse_sensitivity(degrees);
    hostui::set("mod.mouse-camera.sensitivity", std::to_string(degrees));
    mods::packages::remember_option("mouse-camera.sensitivity", degrees);
}
void set_invert_camera_y(bool on) {
    input_map::Mapping m = input_map::current();
    if (m.invert_camera_y == on) return;
    m.invert_camera_y = on;
    input_map::set_current(m);
}

std::vector<std::string> tabs() {
    std::lock_guard lock(g_mu);
    std::vector<std::string> out;
    for (const char* t : kTabOrder)
        for (const Item& it : registry())
            if (it.tab == t) { out.push_back(t); break; }
    for (const Item& it : registry())
        if (std::find(out.begin(), out.end(), it.tab) == out.end()) out.push_back(it.tab);
    return out;
}
std::string tab_label(const std::string& tab) {
    if (tab == "camera") return "Camera";
    if (tab == "controls") return "Controls";
    if (tab == "hud") return "HUD";
    if (tab == "mods") return "Mods";
    return tab;
}
std::vector<const Item*> items(const std::string& tab) {
    std::lock_guard lock(g_mu);
    std::vector<const Item*> out;
    for (const Item& it : registry())
        if (it.tab == tab) out.push_back(&it);
    return out;
}
const Item* find(const std::string& id) {
    std::lock_guard lock(g_mu);
    for (const Item& it : registry())
        if (it.id == id) return &it;
    return nullptr;
}
void add(Item item) {
    std::lock_guard lock(g_mu);
    for (Item& it : registry())
        if (it.id == item.id) { it = std::move(item); return; }
    registry().reserve(64);  // the page keeps pointers between frames: no reallocation in play (prototype)
    registry().push_back(std::move(item));
}

float pending(const std::string& id) {
    std::lock_guard lock(g_mu);
    auto it = g_pending.find(id);
    return it == g_pending.end() ? NAN : it->second;
}

static float current(const Item& item) {
    const float p = pending(item.id);
    return std::isnan(p) ? (item.get ? item.get() : 0.0f) : p;
}

std::string value_text(const Item& item) {
    const float v = current(item);
    switch (item.kind) {
    case Kind::Toggle: return v >= 0.5f ? "On" : "Off";
    case Kind::Choice: {
        const size_t i = size_t(std::clamp(int(std::lround(v)), 0, int(item.choices.size()) - 1));
        return item.choices.empty() ? "" : item.choices[i];
    }
    case Kind::Number:
        if (item.format) return item.format(v);
        char b[32];
        std::snprintf(b, sizeof b, "%.2f", v);
        return b;
    }
    return "";
}

float apply(const Item& item, int direction) {
    const float v = current(item);
    float next = v;
    switch (item.kind) {
    case Kind::Toggle: next = direction == 0 ? (v >= 0.5f ? 0.0f : 1.0f) : direction > 0 ? 1.0f : 0.0f; break;
    case Kind::Choice: {
        const int n = int(item.choices.size());
        if (n) next = float(((int(std::lround(v)) + (direction == 0 ? 1 : direction)) % n + n) % n);
        break;
    }
    case Kind::Number: {
        const int d = direction == 0 ? 1 : direction;
        // snap to the step grid, so a saved in-between value (a slider's 1.37x) lands on a step
        const float steps = std::round((v - item.min) / item.step) + float(d);
        next = std::clamp(item.min + steps * item.step, item.min, item.max);
        break;
    }
    }
    if (next == v) return v;
    {
        std::lock_guard lock(g_mu);
        g_pending[item.id] = next;
    }
    const std::string id = item.id;
    auto set = item.set;
    hostui::post([id, set, next] {
        if (set) set(next);
        std::lock_guard lock(g_mu);
        if (auto it = g_pending.find(id); it != g_pending.end() && it->second == next) g_pending.erase(it);
    });
    return next;
}

void register_builtin() {
    static bool done = false;
    if (done) return;
    done = true;
    {
        Item it;
        it.id = "camera.invert-y", it.tab = "camera", it.label = "Invert camera up/down", it.kind = Kind::Toggle;
        it.help = "The camera, first person and aiming turn the other way up and down.";
        it.get = [] { return input_map::invert_camera_y() ? 1.0f : 0.0f; };
        it.set = [](float v) { set_invert_camera_y(v >= 0.5f); };
        add(std::move(it));
    }
    {
        Item it = builtin_toggle("camera", "direct-camera", "Direct camera",
                                 "The right stick turns the camera at once, without the game's easing.");
        add(std::move(it));
    }
    {
        Item it;
        it.id = "direct-camera.speed", it.tab = "camera", it.label = "Camera speed", it.kind = Kind::Number;
        it.help = "How fast the direct camera turns (1.00x: the game's top speed).";
        it.min = 0.5f, it.max = 2.0f, it.step = 0.25f;
        it.get = [] { return mods::camera_speed(); };
        it.set = [](float v) { set_camera_speed(v); };
        it.format = times;
        it.enabled = [] { return mods::direct_camera(); };
        add(std::move(it));
    }
    add(builtin_toggle("camera", "mouse-camera", "Mouse camera",
                       "Click the game picture to look around with the mouse; Esc lets it go."));
    {
        Item it;
        it.id = "mouse-camera.sensitivity", it.tab = "camera", it.label = "Mouse sensitivity", it.kind = Kind::Number;
        it.help = "How far the camera turns per mouse movement.";
        it.min = 0.08f, it.max = 0.30f, it.step = 0.02f;
        it.get = [] { return mods::mouse_sensitivity(); };
        it.set = [](float v) { set_mouse_sensitivity(v); };
        it.format = [](float v) {
            char b[32];
            std::snprintf(b, sizeof b, "%.2f", v);
            return std::string(b);
        };
        it.enabled = [] { return mods::mouse_camera(); };
        add(std::move(it));
    }
    add(builtin_toggle("camera", "first-person", "First person on R3 / wheel",
                       "R3 or the mouse wheel enters and leaves first person."));
    add(builtin_toggle("camera", "target-letterbox", "Letterbox while targeting",
                       "Black bars at the top and bottom while Link targets, as in the original."));
}

}  // namespace game_settings

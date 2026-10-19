// The world editor's own overlays in the launch mode (editor_shell.h).
#include "editor_shell.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../editor/editor_game.h"
#include "../editor/mcp_game.h"
#include "../editor/scenes.h"
#include "../gfx/renderer.h"
#include "../input_map.h"
#include "../platform/keycodes.h"
#include "editor_panel.h"
#include "editor_dock.h"
#include "editor_resident.h"
#include "../mods/story_enemy_variants.h"
#include "extensions.h"
#include "hostui.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "text_entry.h"
#include "game_objects_palette.h"
#include "layer_escape.h"
#include "overlay.h"
// ImGuizmo needs Dear ImGui declared first
#include "ImGuizmo.h"

namespace editor_shell {
namespace {
using editor::Value;
const ImVec4 kMuted(0.62f, 0.66f, 0.72f, 1), kGood(0.45f, 0.82f, 0.52f, 1), kBad(0.95f, 0.55f, 0.45f, 1), kHead(0.95f, 0.85f, 0.55f, 1);
constexpr double kPi = 3.14159265358979323846;

struct ShellState {
    bool outliner = true, inspector = true, problems = false, test = false, log = false;
    bool story_tab = false;  // show the Outliner's Story tab once
    bool focus_inspector = false, focus_problems = false;
    bool quests = false, flags = false, search = false, focus_quests = false;  // the quest editor's windows
    bool quests_focused = false;  // Ctrl+C / Ctrl+V are the quest editor's there
    bool scenes = false, focus_scenes = false;  // the scene editor's window
    // a walk point dragged with its handle in the picture
    bool point_dragging = false;
    float point_drag[16] = {};
    bool route_dragging = false;  // a route point's handle (route_overlay)
    float route_drag[16] = {};
    int gizmo = 0;           // 0 move, 1 turn
    int grid = 0, angle = 1;  // snapping: indexes into kGrid / kAngle
    std::string placing;      // a type from list_actor_types while placing by click
    std::string place_model;  // its model
    std::string place_resident;  // a resident's type (uo2, um1, ym1) while placing one
    std::string place_variant;   // an enemy's declared variant (story_enemy_variants.h) while placing one
    bool play_restore = true;
    bool dragging = false;    // the gizmo was used last frame
    float drag[16] = {};
    std::string drag_actor;
    std::string drag_mod;                // the dragged actors follow the handle in the game (editor::drag_actors)
    std::vector<std::string> drag_ids;
    bool drag_cancelled = false;         // Esc during the drag: the handle rests until the mouse is let go
    std::vector<std::string> types, type_labels, type_groups;
    std::vector<bool> type_model;
    overlay::game_objects::Palette game_objects;  // the Game objects palette (Add > Game objects)
    Value game_object_list;                       // list_game_objects, asked when the palette opens
    int game_object_asked = -100000;
    bool layout_loaded = false;
    bool reset_layout = false;  // Reset layout: the default docked layout again
    float status_h = 52;        // the status bar's height at the last frame (the dockspace ends above it)
} S;
// "Add a quest talk for this villager...": a game villager clicked in the picture
struct VillagerTalk {
    Value who;               // the list_villagers entry
    int target = 0;          // 0 a new quest, 1 a new step of a quest, 2 a step of a quest
    std::string quest;
    int step = 0;
    char title[96] = {}, line[241] = {};
    std::string error;
    bool running = false, open = false;
} g_villager;
editor_escape::Gate g_escape;  // Esc: the placing, the handle drag or the quest connection first
const float kGrid[] = {0, 10, 50, 100};
const char* const kGridNames[] = {"Grid off", "Grid 10", "Grid 50", "Grid 100"};
const float kAngle[] = {0, 15, 45, 90};
const char* const kAngleNames[] = {"Angle off", "Angle 15", "Angle 45", "Angle 90"};

float num(const Value& v, const char* k, float fallback = 0) { return v.get(k).type == Value::Number ? float(v.get(k).number) : fallback; }

// ---------------------------------------------------------------- the camera: projection into the picture
struct View {
    bool ok = false;
    float eye[3], right[3], up[3], back[3];  // camera axes (back = from the center towards the eye)
    float sx = 1, sy = 1, near_z = 10, far_z = 100000;
    ImVec2 pos, size;  // the picture inside the display
};
void normalize(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f)
        for (int i = 0; i < 3; i++) v[i] /= l;
}
View view_of(float w, float h) {
    View v;
    const editor::CameraView c = editor::camera_view();
    if (!c.valid) return v;
    for (int i = 0; i < 3; i++) {
        v.eye[i] = c.eye[i];
        v.back[i] = c.eye[i] - c.center[i];
    }
    normalize(v.back);
    float upv[3] = {c.up[0], c.up[1], c.up[2]};
    // right = up x back, up = back x right
    v.right[0] = upv[1] * v.back[2] - upv[2] * v.back[1];
    v.right[1] = upv[2] * v.back[0] - upv[0] * v.back[2];
    v.right[2] = upv[0] * v.back[1] - upv[1] * v.back[0];
    normalize(v.right);
    v.up[0] = v.back[1] * v.right[2] - v.back[2] * v.right[1];
    v.up[1] = v.back[2] * v.right[0] - v.back[0] * v.right[2];
    v.up[2] = v.back[0] * v.right[1] - v.back[1] * v.right[0];
    // the scales the game's projection uses (its matrix when it is a plain perspective one)
    if (c.proj[14] == -1 && c.proj[0] > 0 && c.proj[5] > 0) v.sx = c.proj[0], v.sy = c.proj[5];
    else {
        const float t = 1.0f / std::tan(float(c.fovy * kPi / 360.0));
        v.sy = t;
        v.sx = t / std::max(0.1f, c.aspect);
    }
    v.near_z = std::max(1.0f, c.near_z);
    v.far_z = std::max(v.near_z + 1, c.far_z);
    // the picture keeps the projection's shape in the display (pillar or letter boxes around it)
    const float aspect = v.sy / v.sx;
    if (w / h > aspect) v.size = ImVec2(h * aspect, h), v.pos = ImVec2((w - h * aspect) / 2, 0);
    else v.size = ImVec2(w, w / aspect), v.pos = ImVec2(0, (h - w / aspect) / 2);
    v.ok = true;
    return v;
}
bool project(const View& v, const float p[3], ImVec2& out) {
    const float d[3] = {p[0] - v.eye[0], p[1] - v.eye[1], p[2] - v.eye[2]};
    const float x = d[0] * v.right[0] + d[1] * v.right[1] + d[2] * v.right[2];
    const float y = d[0] * v.up[0] + d[1] * v.up[1] + d[2] * v.up[2];
    const float z = -(d[0] * v.back[0] + d[1] * v.back[1] + d[2] * v.back[2]);  // distance in front
    if (z < v.near_z) return false;
    out = ImVec2(v.pos.x + v.size.x * 0.5f * (1 + v.sx * x / z), v.pos.y + v.size.y * 0.5f * (1 - v.sy * y / z));
    return true;
}
void ray(const View& v, ImVec2 m, float dir[3]) {
    const float nx = (m.x - v.pos.x) / v.size.x * 2 - 1, ny = 1 - (m.y - v.pos.y) / v.size.y * 2;
    const float x = nx / v.sx, y = ny / v.sy;
    for (int i = 0; i < 3; i++) dir[i] = v.right[i] * x + v.up[i] * y - v.back[i];
    normalize(dir);
}
// ImGuizmo's matrices (column-major, OpenGL conventions)
void gl_view(const View& v, float m[16]) {
    const float* r = v.right;
    const float* u = v.up;
    const float* b = v.back;
    const float* e = v.eye;
    const float out[16] = {r[0], u[0], b[0], 0, r[1], u[1], b[1], 0, r[2], u[2], b[2], 0,
                           -(r[0] * e[0] + r[1] * e[1] + r[2] * e[2]), -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]), -(b[0] * e[0] + b[1] * e[1] + b[2] * e[2]), 1};
    std::memcpy(m, out, sizeof out);
}
void gl_projection(const View& v, float m[16]) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = v.sx;
    m[5] = v.sy;
    m[10] = -(v.far_z + v.near_z) / (v.far_z - v.near_z);
    m[11] = -1;
    m[14] = -2 * v.far_z * v.near_z / (v.far_z - v.near_z);
}

// ---------------------------------------------------------------- actors in the picture
// a placed actor's collision cylinder (its own for models; the game's usual size for game actors)
void cylinder_of(const Value& a, float& radius, float& height) {
    const std::string actor = a.get("actor").string();
    if (a.get("kind").string() == "mod") {
        radius = num(a.get("collision"), "radius", 30);
        height = num(a.get("collision"), "height", 100);
        return;
    }
    radius = 40, height = 120;
    if (actor == "chest") radius = 60, height = 80;
    else if (actor == "pot" || actor == "barrel" || actor == "crate") radius = 35, height = 70;
    else if (actor == "switch") radius = 60, height = 20;
    else if (actor == "door") radius = 150, height = 300;
    else if (actor == "resident") radius = 40, height = 150;
    const float s = num(a.get("scale"), "y", 1);
    height *= s;
    radius *= num(a.get("scale"), "x", 1);
}
bool position_of(const Value& a, float p[3]) {
    const Value& v = a.get("position");
    if (v.get("x").type != Value::Number) return false;
    p[0] = num(v, "x"), p[1] = num(v, "y"), p[2] = num(v, "z");
    return true;
}
void draw_cylinder(ImDrawList* dl, const View& v, const float p[3], float radius, float height, ImU32 colour) {
    constexpr int kSeg = 28;
    ImVec2 lo[kSeg], hi[kSeg];
    bool ok[kSeg];
    for (int i = 0; i < kSeg; i++) {
        const double t = 2 * kPi * i / kSeg;
        const float b[3] = {p[0] + float(std::sin(t)) * radius, p[1], p[2] + float(std::cos(t)) * radius};
        const float u[3] = {b[0], p[1] + height, b[2]};
        ok[i] = project(v, b, lo[i]) && project(v, u, hi[i]);
    }
    for (int i = 0; i < kSeg; i++) {
        const int j = (i + 1) % kSeg;
        if (!ok[i] || !ok[j]) continue;
        dl->AddLine(lo[i], lo[j], colour, 1.5f);
        dl->AddLine(hi[i], hi[j], colour, 1.5f);
        if (i % 7 == 0) dl->AddLine(lo[i], hi[i], colour, 1.0f);
    }
}
// A world object's or island building's footprint (its world bounds from placement_conflicts at the pose shown,
// also during a handle drag): green when the place is clear, red with what is in the way, in plain words, when not.
struct Footprint {
    std::string key;
    double asked = -1;
    Value result;
};
Footprint g_footprint;
void building_footprint(ImDrawList* dl, const View& v, const std::string& mod, const std::string& id, const float p[3], double yaw) {
    char key[192];
    std::snprintf(key, sizeof key, "%s/%s/%.0f/%.0f/%.0f/%.1f", mod.c_str(), id.c_str(), p[0], p[1], p[2], yaw);
    const double now = ImGui::GetTime();
    // every move while dragging (a few times a second at most; the room's index is cached by the game)
    if (g_footprint.key != key && now - g_footprint.asked > 0.08) {
        Value q, at;
        at["x"] = p[0], at["y"] = p[1], at["z"] = p[2];
        q["id"] = id, q["mod"] = mod, q["position"] = at, q["yaw"] = yaw;
        const Value r = editor::query("preview_collision", q);
        g_footprint = {key, now, r.get("ok").boolean ? r.get("result") : Value()};
    }
    const Value& r = g_footprint.result;
    if (r.get("minimum").get("x").type != Value::Number) return;
    const float lo[3] = {float(num(r.get("minimum"), "x")), float(num(r.get("minimum"), "y")), float(num(r.get("minimum"), "z"))};
    const float hi[3] = {float(num(r.get("maximum"), "x")), float(num(r.get("maximum"), "y")), float(num(r.get("maximum"), "z"))};
    const bool clear = r.get("clear").boolean;
    const ImU32 colour = clear ? IM_COL32(110, 230, 120, 220) : IM_COL32(255, 70, 60, 240);
    ImVec2 c[8];
    bool ok = true;
    for (int k = 0; k < 8; k++) {
        const float q[3] = {k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2]};
        ok = ok && project(v, q, c[k]);
    }
    if (ok) {
        static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& e : edges) dl->AddLine(c[e[0]], c[e[1]], colour, clear ? 1.5f : 2.5f);
    }
    // the footprint's grid on its base: green where it fits, red where it can't stand
    for (int axis = 0; axis < 2; ++axis) {
        const float from = axis ? lo[2] : lo[0], to = axis ? hi[2] : hi[0];
        const int lines = std::clamp(int((to - from) / 200), 1, 24);
        for (int k = 1; k < lines; ++k) {
            const float t = from + (to - from) * k / lines;
            const float a3[3] = {axis ? lo[0] : t, lo[1] + 2, axis ? t : lo[2]}, b3[3] = {axis ? hi[0] : t, lo[1] + 2, axis ? t : hi[2]};
            ImVec2 a2, b2;
            if (project(v, a3, a2) && project(v, b3, b2)) dl->AddLine(a2, b2, (colour & 0x00FFFFFF) | 0x90000000, 1.0f);
        }
    }
    if (clear) return;
    // not in the game while it can't stand here: its shape as a ghost (the collision mesh's edges)
    const auto& points = r.get("points").array;
    std::vector<ImVec2> screen(points.size());
    std::vector<bool> seen(points.size());
    for (size_t k = 0; k < points.size(); ++k) {
        const float q[3] = {float(num(points[k], "x")), float(num(points[k], "y")), float(num(points[k], "z"))};
        seen[k] = project(v, q, screen[k]);
    }
    for (const auto& e : r.get("edges").array) {
        if (e.array.size() != 2) continue;
        const size_t a1 = size_t(e.array[0].number), b1 = size_t(e.array[1].number);
        if (a1 < screen.size() && b1 < screen.size() && seen[a1] && seen[b1]) dl->AddLine(screen[a1], screen[b1], IM_COL32(255, 120, 110, 110), 1.0f);
    }
    const auto& conflicts = r.get("conflicts").array;
    if (conflicts.empty()) return;
    // what is in the way, made visible: a place as a ring of its reach, a route as its line
    const ImU32 marks = IM_COL32(255, 190, 60, 230);
    for (const auto& k : conflicts) {
        if (k.get("path").type == Value::Array) {
            ImVec2 prev;
            bool have = false;
            for (const auto& q : k.get("path").array) {
                const float w[3] = {float(num(q, "x")), float(num(q, "y")) + 10, float(num(q, "z"))};
                ImVec2 here;
                const bool in = project(v, w, here);
                if (in && have) dl->AddLine(prev, here, marks, 3.0f);
                prev = here, have = in;
            }
        } else if (k.get("at").type == Value::Object) {
            const float ax = float(num(k.get("at"), "x")), ay = float(num(k.get("at"), "y")) + 10, az = float(num(k.get("at"), "z"));
            const float radius = float(k.get("reach").number);
            ImVec2 prev;
            bool have = false;
            for (int j = 0; j <= 24; ++j) {
                const float angle = float(j) / 24 * 6.2831853f, w[3] = {ax + radius * std::cos(angle), ay, az + radius * std::sin(angle)};
                ImVec2 here;
                const bool in = project(v, w, here);
                if (in && have) dl->AddLine(prev, here, marks, 2.5f);
                prev = here, have = in;
            }
        }
    }
    // plain words first ("In the way: a door of the island (and 8 more)"); hovering lists every reason with the
    // game's own names, as the MCP error does
    std::string text = "In the way: " + conflicts.front().get("message").string();
    if (conflicts.size() > 1) text += " (and " + std::to_string(conflicts.size() - 1) + " more)";
    // a single small thing: how far to move (100 units are about a metre, Link is about 150 tall)
    else if (conflicts.front().get("move").number > 0)
        text += " - move it about " + std::to_string(int(conflicts.front().get("move").number)) + " units away";
    // under the building's base, where the handle is (its top is often out of the picture)
    const float base[3] = {(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2};
    ImVec2 at;
    if (!project(v, base, at)) return;
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    at.x = std::clamp(at.x - size.x / 2, v.pos.x + 4, v.pos.x + std::max(4.0f, v.size.x - size.x - 4));
    at.y = std::clamp(at.y + 24, v.pos.y + 4, v.pos.y + std::max(4.0f, v.size.y - size.y - 4));
    const ImVec2 from(at.x - 4, at.y - 2), to(at.x + size.x + 4, at.y + size.y + 2);
    dl->AddRectFilled(from, to, IM_COL32(40, 0, 0, 200), 3);
    dl->AddText(at, IM_COL32(255, 200, 190, 255), text.c_str());
    if (ImGui::IsMouseHoveringRect(from, to, false)) {
        std::string all = "In the way here:";
        for (const auto& c : conflicts) {
            all += "\n- " + c.get("message").string();
            if (!c.get("native").string().empty()) all += "  (game name: " + c.get("native").string() + ")";
        }
        ImGui::SetTooltip("%s", all.c_str());
    }
}
std::string current_stage(const Value& state) {
    const Value& fly = state.get("fly");
    if (fly.get("on").boolean) return fly.get("stage").string();
    return state.get("link").get("stage").string();
}

// ---------------------------------------------------------------- docking (the launch mode only): editor_dock.h
using editor_dock::panel;
// The dockspace over the picture (above the status bar), before the panels each frame
void dockspace(float w, float h) { editor_dock::dockspace(ImVec2(w, std::max(100.0f, h - S.status_h)), S.reset_layout); }

// ---------------------------------------------------------------- the handle drag, live in the game
double rounded(double v) { return std::round(v * 10) / 10; }
Value point_of(double x, double y, double z) {
    Value p;
    p["x"] = rounded(x), p["y"] = rounded(y), p["z"] = rounded(z);
    return p;
}
// Moving the group: the selected actor and the others of the multiple selection, by the same offset.
bool group_drag(const std::string& selected) {
    const auto& group = editor_ui::multi_selection();
    return S.gizmo == 0 && group.size() > 1 && std::find(group.begin(), group.end(), selected) != group.end();
}
double drag_degrees() {
    double deg = std::atan2(S.drag[8], S.drag[10]) * 180 / kPi;
    return deg < 0 ? deg + 360 : deg;
}
// The poses the handle gives the dragged actors now: what the game shows while dragging and what the
// released command keeps.
Value drag_poses(const Value& manifest, const Value& sel, const std::string& selected, std::vector<std::string>& ids) {
    Value poses;
    poses.type = Value::Array;
    ids.clear();
    float at[3];
    if (!position_of(sel, at)) return poses;
    if (S.gizmo == 0) {
        const float off[3] = {S.drag[12] - at[0], S.drag[13] - at[1], S.drag[14] - at[2]};
        std::vector<std::string> group = group_drag(selected) ? editor_ui::multi_selection() : std::vector<std::string>{selected};
        for (const auto& a : manifest.get("actors").array) {
            const std::string id = a.get("id").string();
            float p[3];
            if (std::find(group.begin(), group.end(), id) == group.end() || !position_of(a, p)) continue;
            Value pose;
            pose["id"] = id;
            // the selected actor exactly at the handle; the others by the rounded offset, as move_actors does
            pose["position"] = id == selected ? point_of(S.drag[12], S.drag[13], S.drag[14])
                                              : point_of(p[0] + rounded(off[0]), p[1] + rounded(off[1]), p[2] + rounded(off[2]));
            poses.array.push_back(pose);
            ids.push_back(id);
        }
        return poses;
    }
    Value rot = sel.get("rotation").type == Value::Object ? sel.get("rotation") : Value();
    rot["x"] = num(rot, "x");
    rot["y"] = rounded(drag_degrees());
    rot["z"] = num(rot, "z");
    Value pose;
    pose["id"] = selected;
    pose["rotation"] = rot;
    poses.array.push_back(pose);
    ids.push_back(selected);
    return poses;
}
// Esc (or the selection went away) during a drag: the actors go back to the package's places, no command.
void cancel_drag() {
    if (!S.dragging) return;
    editor::end_drag(S.drag_mod, S.drag_ids, true);
    S.dragging = false;
    S.drag_cancelled = true;
    S.drag_actor.clear();
}

// test aid: WWHD_TEST_EDITOR_DRAG=<from>-<to>:<dx>:<dy> drags the selected actor's handle (from its centre)
// by dx, dy picture units between those TV frames, the mouse button held until <to> (screenshots of a live drag)
struct TestDrag {
    uint64_t from = 0, to = 0;
    float dx = 0, dy = 0;
    bool on = false, started = false;
    ImVec2 start;
} g_test_drag = [] {
    TestDrag t;
    unsigned long long a, b;
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_DRAG"); e && std::sscanf(e, "%llu-%llu:%f:%f", &a, &b, &t.dx, &t.dy) == 4 && b > a + 1)
        t.from = a, t.to = b, t.on = true;
    return t;
}();
void test_drag(ImVec2 centre) {
    TestDrag& t = g_test_drag;
    const uint64_t f = render::frame_count();
    if (!t.on || f < t.from || f > t.to + 1) return;
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (!t.started) t.started = true, t.start = centre;
    const float k = std::min(1.0f, float(f - t.from) / float(t.to - t.from));
    // as the host's pointer (the overlay feeds it at the next frame)
    overlay::test_pointer((t.start.x + t.dx * k) / size.x, (t.start.y + t.dy * k) / size.y, f > t.from && f < t.to);
    if (f >= t.to) t.on = false;
}

// ---------------------------------------------------------------- the scene editor in the picture
// The shown scene's camera keys (each eye, a line to what it looks at, lines from key to key), its
// walking paths, and the camera at the timeline's cursor. The selected walk point gets the move handle;
// letting go is one command (set_walk_point). True while that handle shows (the actor's handle waits).
bool point3(const Value& v, float p[3]) {
    if (v.type != Value::Object || v.get("x").type != Value::Number) return false;
    p[0] = num(v, "x"), p[1] = num(v, "y"), p[2] = num(v, "z");
    return true;
}
void line3(ImDrawList* dl, const View& v, const float a[3], const float b[3], ImU32 c, float t = 1.5f) {
    ImVec2 pa, pb;
    if (project(v, a, pa) && project(v, b, pb)) dl->AddLine(pa, pb, c, t);
}
void camera_mark(ImDrawList* dl, const View& v, const float eye[3], const float target[3], ImU32 c, const char* label) {
    ImVec2 pe;
    if (!project(v, eye, pe)) return;
    // the direction it looks, 120 units long, and the view as a small box
    float d[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    normalize(d);
    const float tip[3] = {eye[0] + d[0] * 120, eye[1] + d[1] * 120, eye[2] + d[2] * 120};
    line3(dl, v, eye, tip, c, 2.5f);
    line3(dl, v, eye, target, (c & 0x00FFFFFFu) | 0x50000000u, 1);
    dl->AddRectFilled(ImVec2(pe.x - 7, pe.y - 5), ImVec2(pe.x + 7, pe.y + 5), c, 2);
    if (label && *label) dl->AddText(ImVec2(pe.x + 9, pe.y - 8), c, label);
}
bool scene_overlay(ImDrawList* dl, const View& v, const Value& m, bool& handle_busy) {
    handle_busy = false;
    std::string scene;
    int step = 0, point = 0;
    double scrub = -1;
    if (!S.scenes || !editor_ui::scene_picture(scene, step, point, scrub)) return false;
    const Value* sc = nullptr;
    for (const auto& x : m.get("scenes").array)
        if (x.get("id").string() == scene) sc = &x;
    if (!sc) return false;
    const auto& steps = sc->get("steps").array;
    for (size_t i = 0; i < steps.size(); i++) {
        const Value& st = steps[i];
        const std::string t = st.get("type").string();
        const bool sel = int(i + 1) == step;
        const ImU32 cam = sel ? IM_COL32(255, 220, 90, 255) : IM_COL32(110, 175, 245, 200);
        char label[48];
        float e[3], g[3];
        if (t == "camera" && point3(st.get("eye"), e)) {
            if (!point3(st.get("target"), g)) std::memcpy(g, e, sizeof g), g[2] += 1;
            std::snprintf(label, sizeof label, "%zu", i + 1);
            camera_mark(dl, v, e, g, cam, label);
            float e2[3], g2[3];
            if (st.get("pan").boolean && point3(st.get("eye_to"), e2) && point3(st.get("target_to"), g2)) {
                line3(dl, v, e, e2, cam, 2);
                std::snprintf(label, sizeof label, "%zu end", i + 1);
                camera_mark(dl, v, e2, g2, cam, label);
            }
        } else if (t == "camera_path") {
            const auto& keys = st.get("points").array;
            float prev[3] = {};
            for (size_t k = 0; k < keys.size(); k++) {
                if (!point3(keys[k].get("eye"), e) || !point3(keys[k].get("target"), g)) continue;
                if (k) line3(dl, v, prev, e, cam, 2.5f);
                std::snprintf(label, sizeof label, "%zu.%zu", i + 1, k + 1);
                camera_mark(dl, v, e, g, cam, label);
                std::memcpy(prev, e, sizeof prev);
            }
        } else if (t == "walk") {
            const auto& pts = st.get("points").array;
            const ImU32 c = sel ? IM_COL32(150, 240, 150, 255) : IM_COL32(110, 200, 120, 180);
            float prev[3] = {};
            for (size_t k = 0; k < pts.size(); k++) {
                if (!point3(pts[k], e)) continue;
                if (k) line3(dl, v, prev, e, c, 2.5f);
                ImVec2 pp;
                if (project(v, e, pp)) {
                    const bool me = sel && int(k + 1) == point;
                    dl->AddCircleFilled(pp, me ? 7.0f : 5.0f, me ? IM_COL32(255, 230, 120, 255) : c);
                    std::snprintf(label, sizeof label, "%zu", k + 1);
                    dl->AddText(ImVec2(pp.x + 8, pp.y - 8), c, label);
                }
                std::memcpy(prev, e, sizeof prev);
            }
        }
    }
    // the camera at the timeline's cursor
    if (scrub >= 0) {
        editor::WorldPoint eye, target;
        size_t at = 0;
        std::string why;
        if (editor::scene_camera_at(*sc, scrub, eye, target, at, why)) {
            const float e[3] = {eye.x, eye.y, eye.z}, g[3] = {target.x, target.y, target.z};
            char label[48];
            std::snprintf(label, sizeof label, "%.2fs", scrub / 60);
            camera_mark(dl, v, e, g, IM_COL32(255, 120, 80, 255), label);
        }
    }
    // the selected walk point's handle
    if (step < 1 || step > int(steps.size()) || steps[size_t(step - 1)].get("type").string() != "walk") return false;
    const auto& pts = steps[size_t(step - 1)].get("points").array;
    float p[3];
    if (point < 1 || point > int(pts.size()) || !point3(pts[size_t(point - 1)], p)) return false;
    float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, p[0], p[1], p[2], 1};
    if (S.point_dragging) std::memcpy(model, S.point_drag, sizeof model);
    float view[16], proj[16];
    gl_view(v, view);
    gl_projection(v, proj);
    ImGuizmo::SetDrawlist(dl);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetRect(v.pos.x, v.pos.y, v.size.x, v.size.y);
    float snap_values[3] = {kGrid[S.grid], kGrid[S.grid], kGrid[S.grid]};
    ImGuizmo::Manipulate(view, proj, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, model, nullptr, S.grid > 0 ? snap_values : nullptr);
    handle_busy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    if (ImGuizmo::IsUsing()) {
        S.point_dragging = true;
        std::memcpy(S.point_drag, model, sizeof model);
    } else if (S.point_dragging) {
        S.point_dragging = false;
        Value prm;
        prm["scene"] = scene;
        prm["step"] = step;
        prm["point"] = point;
        prm["position"] = point_of(S.point_drag[12], S.point_drag[13], S.point_drag[14]);
        editor_ui::command("set_walk_point", prm);
    }
    return true;
}

// ---------------------------------------------------------------- the selected actor's route in the picture
// Its line (green; segments the ground check marked in red), its points (numbered; a resident starts at
// its position), and the selected point's move handle: letting go is one command (move_route_point).
// A click on a point selects it. True while that handle shows (the actor's handle waits).
bool route_overlay(ImDrawList* dl, const View& v, const Value* sel, bool& handle_busy) {
    handle_busy = false;
    std::string id;
    int point = 0;
    std::vector<std::pair<int, int>> bad;
    if (!sel || !editor_ui::route_picture(id, point, bad) || sel->get("id").string() != id) return false;
    const Value& path = sel->get("path");
    if (path.type != Value::Object) return false;
    const bool resident = sel->get("actor").string() == "resident";
    // the numbered points: a resident's 0 is its position
    std::vector<std::array<float, 3>> pts;
    std::vector<int> numbers;
    float q[3];
    if (resident && position_of(*sel, q)) pts.push_back({q[0], q[1], q[2]}), numbers.push_back(0);
    int n = 0;
    for (const auto& pt : path.get("points").array) {
        ++n;
        if (point3(pt, q)) pts.push_back({q[0], q[1], q[2]}), numbers.push_back(n);
    }
    const std::string mode = path.get("mode").string("once");
    auto marked = [&](int a, int b) {
        for (const auto& [f, t] : bad)
            if (f == a && t == b) return true;
        return false;
    };
    const ImU32 green = IM_COL32(110, 230, 120, 230), red = IM_COL32(245, 90, 80, 240);
    auto segment = [&](size_t i, size_t j) {
        const float lift = 8;  // a little above the ground, so the line shows
        const float a[3] = {pts[i][0], pts[i][1] + lift, pts[i][2]}, b[3] = {pts[j][0], pts[j][1] + lift, pts[j][2]};
        line3(dl, v, a, b, marked(numbers[i], numbers[j]) ? red : green, 3);
    };
    for (size_t i = 1; i < pts.size(); i++) segment(i - 1, i);
    if (mode == "loop" && pts.size() > 2) segment(pts.size() - 1, 0);
    // points: dots, numbers; a click selects one
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int clicked = -1;
    for (size_t i = 0; i < pts.size(); i++) {
        ImVec2 at;
        if (!project(v, pts[i].data(), at)) continue;
        const bool me = numbers[i] == point && numbers[i] > 0;
        dl->AddCircleFilled(at, me ? 8.0f : 6.0f, me ? IM_COL32(255, 230, 120, 255) : numbers[i] == 0 ? IM_COL32(200, 200, 200, 230) : green);
        char label[24];
        std::snprintf(label, sizeof label, numbers[i] == 0 ? "start" : "%d", numbers[i]);
        dl->AddText(ImVec2(at.x + 9, at.y - 9), IM_COL32(240, 240, 240, 230), label);
        if (numbers[i] > 0 && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered() && std::hypot(mouse.x - at.x, mouse.y - at.y) < 10) clicked = numbers[i];
    }
    if (clicked > 0 && !ImGuizmo::IsOver()) editor_ui::route_select_point(clicked), point = clicked;
    if (point < 1 || point > n) return false;
    const auto& sp = path.get("points").array[size_t(point - 1)];
    if (!point3(sp, q)) return false;
    float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, q[0], q[1], q[2], 1};
    if (S.route_dragging) std::memcpy(model, S.route_drag, sizeof model);
    float view[16], proj[16];
    gl_view(v, view);
    gl_projection(v, proj);
    ImGuizmo::SetDrawlist(dl);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetRect(v.pos.x, v.pos.y, v.size.x, v.size.y);
    float snap_values[3] = {kGrid[S.grid], kGrid[S.grid], kGrid[S.grid]};
    ImGuizmo::PushID(0x52545054);  // its own handle, apart from the actor's
    ImGuizmo::Manipulate(view, proj, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, model, nullptr, S.grid > 0 ? snap_values : nullptr);
    handle_busy = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
    if (ImGuizmo::IsUsing()) {
        S.route_dragging = true;
        std::memcpy(S.route_drag, model, sizeof model);
    } else if (S.route_dragging) {
        S.route_dragging = false;
        Value prm;
        prm["id"] = id;
        prm["point"] = point;
        prm["position"] = point_of(S.route_drag[12], S.route_drag[13], S.route_drag[14]);
        editor_ui::command("move_route_point", prm);
    }
    ImGuizmo::PopID();
    return true;
}

// ---------------------------------------------------------------- a game villager's quest talk
// "Add a quest talk for this villager...": a new quest with the talk as its first step, a new step of a
// quest, or a step's talk; one command (add_villager_talk), one undo step; then the quest editor opens at
// that step. The villager itself (its own dialogue, quests, save flags) is never changed.
void villager_popup(const editor_ui::Snapshot& snap) {
    auto& V = g_villager;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(520.0f, vp->WorkSize.x - 32), 0));
    if (!ImGui::BeginPopupModal("Add a quest talk", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    ImGui::TextColored(kHead, "%s", V.who.get("name").string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s, %s)", V.who.get("id").string().c_str(), V.who.get("where").string().c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextWrapped("Adds a quest talk to %s: while that quest step is current, this villager says your line once. "
                       "The villager's own dialogue, quests and save flags stay as they are.",
                       snap.open.c_str());
    ImGui::PopStyleColor();
    const Value& quests = snap.manifest.get("quests");
    if (quests.array.empty()) V.target = 0;
    ImGui::RadioButton("New quest...", &V.target, 0);
    ImGui::BeginDisabled(quests.array.empty());
    ImGui::SameLine();
    ImGui::RadioButton("New step of a quest", &V.target, 1);
    ImGui::SameLine();
    ImGui::RadioButton("A step of a quest", &V.target, 2);
    ImGui::EndDisabled();
    if (V.target == 0) {
        ImGui::TextUnformatted("Title");
        ImGui::SetNextItemWidth(-FLT_MIN);
        text_entry::field("##villager_title", V.title, sizeof V.title, 0, "the quest's title");
    } else {
        if (V.quest.empty() || !std::any_of(quests.array.begin(), quests.array.end(), [&](const Value& q) { return q.get("id").string() == V.quest; }))
            V.quest = quests.array.empty() ? std::string() : quests.array[0].get("id").string(), V.step = 0;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##villager_quest", V.quest.c_str())) {
            for (const auto& q : quests.array)
                if (ImGui::Selectable((q.get("title").string() + "  (" + q.get("id").string() + ")").c_str(), q.get("id").string() == V.quest))
                    V.quest = q.get("id").string(), V.step = 0;
            ImGui::EndCombo();
        }
        if (V.target == 2) {
            const Value* q = nullptr;
            for (const auto& qq : quests.array)
                if (qq.get("id").string() == V.quest) q = &qq;
            std::string shown = V.step ? "step " + std::to_string(V.step) : "(choose a step)";
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (q && ImGui::BeginCombo("##villager_step", shown.c_str())) {
                for (const auto& st : q->get("steps").array) {
                    const int n = int(num(st, "n"));
                    const std::string label = "step " + std::to_string(n) + ": " + st.get("text").string() +
                                              (st.get("npc").type == Value::Object ? "  (has a talk: replaced)" : "");
                    if (ImGui::Selectable(label.c_str(), n == V.step)) V.step = n;
                }
                ImGui::EndCombo();
            }
        }
    }
    ImGui::TextUnformatted("What the villager says");
    ImGui::SetNextItemWidth(-FLT_MIN);
    text_entry::field("##villager_line", V.line, sizeof V.line, 0, "one message box, up to 240 bytes");
    if (!V.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("%s", V.error.c_str());
        ImGui::PopStyleColor();
    }
    const bool ready = !V.running && V.line[0] && (V.target == 0 ? V.title[0] != 0 : !V.quest.empty() && (V.target == 1 || V.step > 0));
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button(V.running ? "Adding..." : "Add the talk")) {
        Value p;
        p["who"] = V.who.get("id");
        p["lines"] = std::string(V.line);
        if (V.target == 0) p["title"] = std::string(V.title);
        else p["quest"] = V.quest;
        if (V.target == 2) p["step"] = V.step;
        V.running = true;
        V.error.clear();
        editor_ui::command("add_villager_talk", p, [](const Value& r) {
            g_villager.running = false;
            if (!r.get("ok").boolean) {
                g_villager.error = r.get("error").string();
                return;
            }
            g_villager.open = false;
            editor_ui::show_quest(r.get("result").get("quest").string(), "step:" + std::to_string(int(r.get("result").get("step").number)));
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) V.open = false;
    if (!V.open) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- the picture: labels, cylinders, handles, clicks
void world(float w, float h) {
    const editor_ui::Snapshot& snap = editor_ui::snapshot();
    const View v = view_of(w, h);
    editor_dock::begin_picture(w, h);  // at the back: the dockspace and the panels stay in front of it
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered();
    // look with the right button held, the wheel for the speed
    if (hovered && ImGui::IsMouseDown(1)) editor::add_look(io.MouseDelta.x, io.MouseDelta.y);
    if (hovered && io.MouseWheel != 0) editor::add_speed(io.MouseWheel);
    if (!v.ok) {
        ImGui::End();
        return;
    }
    const std::string stage = current_stage(snap.state);
    const std::string selected = editor_ui::selected();
    struct Label {
        std::string id;
        ImVec2 at;
        float depth;
    };
    std::vector<Label> labels;
    const Value* sel = nullptr;
    for (const auto& a : snap.manifest.get("actors").array) {
        float p[3];
        if (a.get("stage").string() != stage || !position_of(a, p)) continue;
        const float d2 = (p[0] - v.eye[0]) * (p[0] - v.eye[0]) + (p[1] - v.eye[1]) * (p[1] - v.eye[1]) + (p[2] - v.eye[2]) * (p[2] - v.eye[2]);
        if (d2 > 12000.0f * 12000.0f) continue;
        float radius, height;
        cylinder_of(a, radius, height);
        const float top[3] = {p[0], p[1] + height, p[2]};
        ImVec2 at;
        if (!project(v, top, at)) continue;
        const std::string id = a.get("id").string();
        labels.push_back({id, at, std::sqrt(d2)});
        if (id == selected) sel = &a;
    }
    // the game's own villagers the NPC catalogue knows (not the mod's): their names, a click offers a quest talk
    std::vector<Label> villagers;
    std::vector<const Value*> villager_of;
    for (const auto& vg : snap.villagers.array) {
        float p[3];
        if (vg.get("stage").string() != stage || !position_of(vg, p)) continue;
        const float d2 = (p[0] - v.eye[0]) * (p[0] - v.eye[0]) + (p[1] - v.eye[1]) * (p[1] - v.eye[1]) + (p[2] - v.eye[2]) * (p[2] - v.eye[2]);
        if (d2 > 8000.0f * 8000.0f) continue;
        const float top[3] = {p[0], p[1] + 170, p[2]};
        ImVec2 at;
        if (!project(v, top, at)) continue;
        villagers.push_back({vg.get("name").string(), at, std::sqrt(d2)});
        villager_of.push_back(&vg);
    }
    // the other selected actors: their cylinders
    for (const auto& a : snap.manifest.get("actors").array) {
        const auto& group = editor_ui::multi_selection();
        const std::string id = a.get("id").string();
        float p[3], radius, height;
        if (id == selected || std::find(group.begin(), group.end(), id) == group.end() || a.get("stage").string() != stage || !position_of(a, p)) continue;
        cylinder_of(a, radius, height);
        draw_cylinder(dl, v, p, radius, height, IM_COL32(255, 210, 90, 140));
    }
    // the scene editor's cameras and walking paths; a walk point's handle comes before the actor's
    bool point_handle_busy = false;
    bool point_handle = scene_overlay(dl, v, snap.manifest, point_handle_busy);
    // the selected actor's route: its line, points and the selected point's handle (before the actor's handle)
    bool route_handle_busy = false;
    if (!point_handle && route_overlay(dl, v, sel, route_handle_busy)) point_handle = true, point_handle_busy = point_handle_busy || route_handle_busy;
    // the selected actor: its cylinder and handles
    float model[16] = {};
    bool gizmo = false;
    if (S.dragging && (!sel || S.drag_actor != selected)) cancel_drag();  // the selection went away mid-drag
    if (S.drag_cancelled && !ImGui::IsMouseDown(0)) S.drag_cancelled = false;
    if (sel) {
        float p[3], radius, height;
        position_of(*sel, p);
        cylinder_of(*sel, radius, height);
        draw_cylinder(dl, v, p, radius, height, IM_COL32(255, 210, 90, 230));
        if (ImVec2 centre; g_test_drag.on && project(v, p, centre)) test_drag(centre);
        const double yaw = num(sel->get("rotation"), "y") * kPi / 180;
        const float c = float(std::cos(yaw)), s = float(std::sin(yaw));
        const float m[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, p[0], p[1], p[2], 1};
        std::memcpy(model, S.dragging && S.drag_actor == selected ? S.drag : m, sizeof model);
        // an island building or another world object (a behaviour): its footprint where it is shown
        if (sel->get("actor").string().rfind("piece:", 0) == 0 || !sel->get("behaviour").string().empty()) {
            const float at[3] = {model[12], model[13], model[14]};
            building_footprint(dl, v, snap.open, selected, at, std::round(std::atan2(model[8], model[0]) * 180 / kPi * 10) / 10);
        }
        float view[16], proj[16];
        gl_view(v, view);
        gl_projection(v, proj);
        ImGuizmo::SetDrawlist(dl);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(v.pos.x, v.pos.y, v.size.x, v.size.y);
        float snap_values[3] = {kGrid[S.grid], kGrid[S.grid], kGrid[S.grid]};
        if (S.gizmo == 1) snap_values[0] = kAngle[S.angle];
        const bool snapping = S.gizmo == 0 ? S.grid > 0 : S.angle > 0;
        // after an Esc the handle rests (not interactive) until the mouse button is let go
        if (!S.drag_cancelled && !point_handle) {
            ImGuizmo::Manipulate(view, proj, S.gizmo == 0 ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE_Y, ImGuizmo::WORLD, model, nullptr,
                                 snapping ? snap_values : nullptr);
            gizmo = true;
        }
        const bool using_now = gizmo && ImGuizmo::IsUsing();
        if (using_now) {
            S.dragging = true;
            S.drag_actor = selected;
            S.drag_mod = snap.open;
            std::memcpy(S.drag, model, sizeof model);
            const float q[3] = {model[12], model[13], model[14]};
            draw_cylinder(dl, v, q, radius, height, IM_COL32(120, 220, 255, 230));
            // live: the game shows the actors where the handle is (once per logic step; no command, no undo step)
            editor::drag_actors(S.drag_mod, drag_poses(snap.manifest, *sel, selected, S.drag_ids));
        } else if (S.dragging && S.drag_actor == selected && gizmo) {
            // released: one command, one undo step, one log entry
            S.dragging = false;
            std::vector<std::string> ids;
            const Value poses = drag_poses(snap.manifest, *sel, selected, ids);
            const std::string mod = S.drag_mod;
            editor::end_drag(mod, ids, false);
            // a refused or empty change: the game shows the package's places again
            auto put_back = [mod, ids](const Value& r) {
                if (!r.get("ok").boolean || !r.get("result").get("changed").boolean) editor::end_drag(mod, ids, true);
            };
            Value prm;
            prm["id"] = selected;
            if (group_drag(selected)) {
                float at[3];
                position_of(*sel, at);
                Value many;
                many.type = Value::Array;
                for (const auto& id : editor_ui::multi_selection()) many.array.push_back(Value(id));
                Value mp;
                mp["ids"] = many;
                mp["offset"] = point_of(S.drag[12] - at[0], S.drag[13] - at[1], S.drag[14] - at[2]);
                editor_ui::command("move_actors", mp, put_back);
            } else if (S.gizmo == 0) {
                prm["position"] = point_of(S.drag[12], S.drag[13], S.drag[14]);
                editor_ui::command("move_actor", prm, put_back);
            } else {
                prm["rotation"] = poses.array.empty() ? Value() : poses.array[0].get("rotation");
                editor_ui::command("rotate_actor", prm, put_back);
            }
        }
    }
    // labels: far ones fainter; the selected one highlighted
    std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.depth > b.depth; });
    const auto& multi = editor_ui::multi_selection();
    for (const auto& l : labels) {
        const bool me = l.id == selected || std::find(multi.begin(), multi.end(), l.id) != multi.end();
        const ImVec2 sz = ImGui::CalcTextSize(l.id.c_str());
        const ImVec2 a(l.at.x - sz.x / 2 - 4, l.at.y - sz.y - 8), b(l.at.x + sz.x / 2 + 4, l.at.y - 6);
        dl->AddRectFilled(a, b, me ? IM_COL32(120, 90, 20, 220) : IM_COL32(20, 24, 30, l.depth > 6000 ? 110 : 180), 4);
        dl->AddText(ImVec2(a.x + 4, a.y + 1), me ? IM_COL32(255, 230, 160, 255) : IM_COL32(220, 225, 235, 255), l.id.c_str());
        dl->AddCircleFilled(l.at, 3, me ? IM_COL32(255, 210, 90, 255) : IM_COL32(200, 210, 225, 220));
    }
    // the game's villagers: a teal label with the catalogue's name; hovering says what a click does
    const ImVec2 mouse = io.MousePos;
    int villager_hit = -1;
    float villager_d = 26 * 26;
    for (size_t i = 0; i < villagers.size(); i++) {
        const auto& l = villagers[i];
        const ImVec2 sz = ImGui::CalcTextSize(l.id.c_str());
        const ImVec2 a(l.at.x - sz.x / 2 - 4, l.at.y - sz.y - 8), b(l.at.x + sz.x / 2 + 4, l.at.y - 6);
        dl->AddRectFilled(a, b, IM_COL32(16, 70, 74, l.depth > 5000 ? 120 : 200), 4);
        dl->AddText(ImVec2(a.x + 4, a.y + 1), IM_COL32(170, 240, 235, 255), l.id.c_str());
        dl->AddCircleFilled(l.at, 3, IM_COL32(120, 220, 210, 220));
        const float dx = l.at.x - mouse.x, dy = l.at.y - 12 - mouse.y, d = dx * dx + dy * dy;
        if (d < villager_d) villager_d = d, villager_hit = int(i);
    }
    if (hovered && villager_hit >= 0 && S.placing.empty()) {
        const Value& vg = *villager_of[size_t(villager_hit)];
        ImGui::SetTooltip("%s (%s): one of the game's villagers\nClick: Add a quest talk for this villager...", vg.get("name").string().c_str(),
                          vg.get("id").string().c_str());
    }
    // a click: place the chosen type on the ground there, or select the nearest label (or nothing)
    const bool over_gizmo = (gizmo && (ImGuizmo::IsOver() || ImGuizmo::IsUsing())) || point_handle_busy;
    if (hovered && !over_gizmo && ImGui::IsMouseClicked(0)) {
        const ImVec2 m = io.MousePos;
        if (!S.placing.empty()) {
            float dir[3];
            ray(v, m, dir);
            Value prm, o, d;
            o["x"] = v.eye[0], o["y"] = v.eye[1], o["z"] = v.eye[2];
            d["x"] = dir[0], d["y"] = dir[1], d["z"] = dir[2];
            prm["origin"] = o;
            prm["direction"] = d;
            const std::string type = S.placing, model_id = S.place_model, resident = S.place_resident, variant = S.place_variant;
            editor_ui::command("pick", prm, [type, model_id, resident, variant](const Value& r) {
                if (!r.get("ok").boolean) return;
                Value add;
                add["type"] = type;
                add["at"] = "position";
                add["position"] = r.get("result").get("position");
                if (r.get("result").get("stage").type == Value::String) add["stage"] = r.get("result").get("stage");
                if (r.get("result").get("room").type == Value::Number) add["room"] = r.get("result").get("room");
                if (!model_id.empty()) add["model"] = model_id;
                if (type == "resident" && !resident.empty()) add["options"]["resident"] = resident;
                if (!variant.empty()) add["options"]["variant"] = variant;
                editor_ui::command("add_actor", add, [](const Value& a) {
                    if (a.get("ok").boolean) editor_ui::selected() = a.get("result").get("id").string();
                });
            });
            if (!io.KeyShift) S.placing.clear();  // Shift: place more of the same
        } else {
            const Label* best = nullptr;
            float best_d = 26 * 26;
            for (const auto& l : labels) {
                const float dx = l.at.x - m.x, dy = l.at.y - 12 - m.y, d = dx * dx + dy * dy;
                if (d < best_d) best_d = d, best = &l;
            }
            if (villager_hit >= 0 && villager_d < best_d && !snap.open.empty()) {  // a game villager: offer a quest talk
                g_villager = VillagerTalk();
                g_villager.who = *villager_of[size_t(villager_hit)];
                g_villager.open = true;
                ImGui::OpenPopup("Add a quest talk");
            } else if (auto& many = editor_ui::multi_selection(); (io.KeyCtrl || io.KeySuper || io.KeyShift) && best) {  // more than one: Ctrl (Cmd) or Shift adds or takes away
                if (many.empty() && !editor_ui::selected().empty()) many.push_back(editor_ui::selected());
                const auto it = std::find(many.begin(), many.end(), best->id);
                if (it != many.end()) many.erase(it);
                else many.push_back(best->id);
                editor_ui::selected() = many.empty() ? std::string() : many.back();
            } else {
                editor_ui::selected() = best ? best->id : std::string();
                many.clear();
                if (best) many.push_back(best->id);
            }
            editor_ui::focus_field().clear();
        }
    }
    if (!S.placing.empty()) {
        dl->AddText(ImVec2(io.MousePos.x + 14, io.MousePos.y + 10), IM_COL32(255, 230, 160, 255), ("place " + S.placing).c_str());
    }
    villager_popup(snap);
    ImGui::End();
}

// ---------------------------------------------------------------- windows
void add_menu() {
    if (S.types.empty()) {
        editor_ui::command("list_actor_types", Value(), [](const Value& r) {
            if (!r.get("ok").boolean) return;
            S.types.clear(), S.type_labels.clear(), S.type_groups.clear(), S.type_model.clear();
            for (const auto& t : r.get("result").array) {
                S.types.push_back(t.get("type").string());
                S.type_labels.push_back(t.get("label").string() + (t.get("live").boolean ? "" : "  (after a restart)"));
                S.type_groups.push_back(t.get("group").string());
                S.type_model.push_back(t.get("needs_model").boolean);
            }
        });
        S.types.push_back("");  // asked once
    }
    const Value& m = editor_ui::snapshot().manifest;
    std::string group;
    for (size_t i = 0; i < S.types.size(); i++) {
        if (S.types[i].empty()) continue;
        if (S.type_groups[i] != group) {
            group = S.type_groups[i];
            ImGui::SeparatorText(group.c_str());
        }
        if (S.types[i] == "resident") {  // a submenu of its types (what each looks like)
            std::string code;
            if (editor_resident::menu(S.type_labels[i].c_str(), code)) S.placing = S.types[i], S.place_model.clear(), S.place_resident = code, S.place_variant.clear();
            continue;
        }
        if (story_enemy_variants::has_variants(S.types[i])) {  // a submenu of its declared variants
            if (ImGui::BeginMenu(S.type_labels[i].c_str())) {
                for (const auto& v : story_enemy_variants::kVariants) {
                    if (S.types[i] != v.kind) continue;
                    if (ImGui::MenuItem(v.label)) S.placing = S.types[i], S.place_model.clear(), S.place_resident.clear(), S.place_variant = v.name;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", v.help);
                }
                ImGui::EndMenu();
            }
            continue;
        }
        if (!S.type_model[i]) {
            if (ImGui::MenuItem(S.type_labels[i].c_str())) S.placing = S.types[i], S.place_model.clear(), S.place_resident.clear(), S.place_variant.clear();
            continue;
        }
        if (ImGui::BeginMenu(S.type_labels[i].c_str())) {
            for (const auto& md : m.get("models").array) {
                const std::string id = md.get("id").string();
                if (ImGui::MenuItem((id + "  (" + md.get("kind").string("static") + ")").c_str())) S.placing = S.types[i], S.place_model = id, S.place_resident.clear(), S.place_variant.clear();
            }
            if (m.get("models").array.empty()) ImGui::TextDisabled("no models yet: import one (Outliner > Models)");
            ImGui::EndMenu();
        }
    }
    // the game's own objects, by reference (the player's game files have them)
    ImGui::SeparatorText("Game objects");
    if (ImGui::BeginMenu("Game objects")) {
        if (ImGui::GetFrameCount() - S.game_object_asked > 300) {  // asked when opened (the game installs its kinds after start-up)
            S.game_object_asked = ImGui::GetFrameCount();
            editor_ui::command("list_game_objects", Value(), [](const Value& r) {
                if (r.get("ok").boolean) S.game_object_list = r.get("result");
            });
        }
        const std::string type = overlay::game_objects::draw(S.game_objects, S.game_object_list);
        if (!type.empty()) {
            S.placing = type, S.place_model.clear(), S.place_resident.clear(), S.place_variant.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    ImGui::TextDisabled("then click into the picture (Shift: more than one)");
}

void toolbar(float w) {
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(w - 16, 1260.0f), 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    panel();
    ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);
    editor_ui::mod_bar();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (S.placing.empty() ? ImGui::Button("Add") : ImGui::Button("Stop placing")) {
        if (S.placing.empty()) ImGui::OpenPopup("add_menu");
        else S.placing.clear();
    }
    if (ImGui::BeginPopup("add_menu")) {
        add_menu();
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    const char* const modes[] = {"Move", "Turn"};
    ImGui::Combo("##gizmo", &S.gizmo, modes, 2);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(95);
    ImGui::Combo("##grid", &S.grid, kGridNames, 4);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::Combo("##angle", &S.angle, kAngleNames, 4);
    // second row: play and test, the panels
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.25f, 1));
    if (ImGui::Button("Play")) {
        Value p;
        p["from"] = "camera";
        p["restore"] = S.play_restore;
        editor_ui::command("play", p);
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play as Link from the camera's place (Tab)");
    ImGui::SameLine();
    if (ImGui::Button("from Link")) {
        Value p;
        p["from"] = "link";
        p["restore"] = S.play_restore;
        editor_ui::command("play", p);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Restore at Stop", &S.play_restore);
    ImGui::SameLine();
    bool test = editor_ui::snapshot().state.get("test_mode").boolean;
    if (ImGui::Checkbox("Test mode", &test)) {
        Value p;
        p["on"] = test;
        editor_ui::command("test_mode", p);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Outliner", &S.outliner);
    ImGui::SameLine();
    ImGui::Checkbox("Inspector", &S.inspector);
    ImGui::SameLine();
    ImGui::Checkbox("Problems & export", &S.problems);
    ImGui::SameLine();
    ImGui::Checkbox("Camera & test", &S.test);
    ImGui::SameLine();
    ImGui::Checkbox("Log", &S.log);
    ImGui::SameLine();
    ImGui::Checkbox("Quests", &S.quests);
    ImGui::SameLine();
    ImGui::Checkbox("Scenes", &S.scenes);
    ImGui::SameLine();
    ImGui::Checkbox("Flags", &S.flags);
    ImGui::SameLine();
    ImGui::Checkbox("Search", &S.search);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset layout")) S.reset_layout = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The panels back to their default places (docked at the edges)");
    ImGui::SameLine();
    ImGui::TextDisabled("  Esc: cancel placing, else Settings");
    ImGui::End();
}

void status_bar(float w, float h) {
    const editor_ui::Snapshot& snap = editor_ui::snapshot();
    ImGui::SetNextWindowPos(ImVec2(0, h), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::Begin("##editor_status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    S.status_h = ImGui::GetWindowHeight();
    const Value& fly = snap.state.get("fly");
    const Value& link = snap.state.get("link");
    ImGui::Text("%s room %d   camera %.0f %.0f %.0f   speed %.0f   Link %.0f %.0f %.0f", fly.get("stage").string(link.get("stage").string()).c_str(),
                int(num(link, "room")), num(fly.get("position"), "x"), num(fly.get("position"), "y"), num(fly.get("position"), "z"), num(fly, "speed"),
                num(link.get("position"), "x"), num(link.get("position"), "y"), num(link.get("position"), "z"));
    bool ok = true;
    const std::string& message = editor_ui::message(ok);
    const std::string status = editor::status();
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (editor_ui::busy()) ImGui::TextColored(kHead, "working...  ");
    else if (!message.empty()) ImGui::TextColored(ok ? kGood : kBad, "%s", message.c_str());
    if (S.dragging) {
        // the handle drag: live in the game, or what waits (restart-only kinds keep the release behaviour)
        const std::string note = editor::drag_note();
        ImGui::SameLine();
        if (note.empty()) ImGui::TextColored(kGood, "  Moving live in the game (Esc cancels)");
        else ImGui::TextColored(kHead, "  %s", note.c_str());
    } else if (!status.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "  %s", status.c_str());
    }
    if (const std::string quest = editor_ui::quest_status(); !quest.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextColored(kHead, "%s", quest.c_str());
    }
    if (editor_mcp::available()) {
        const std::string mcp = editor_mcp::status_line();
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), w - ImGui::CalcTextSize(mcp.c_str()).x - 16));
        ImGui::TextColored(mcp == "MCP: off" ? kMuted : kGood, "%s", mcp.c_str());
    }
    ImGui::TextColored(kMuted, "%s", S.placing.empty() ? "W A S D fly   E / Q up / down   right mouse look   wheel speed   click select   Tab play"
                                                       : "click: place   Shift+click: place more   Esc: stop placing");
    ImGui::End();
}

void panels(float w, float h) {
    const editor_ui::Snapshot& snap = editor_ui::snapshot();
    if (S.outliner) {
        ImGui::SetNextWindowPos(ImVec2(8, 110), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, h * 0.62f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        panel();
        if (ImGui::Begin("Outliner", &S.outliner)) {
            if (ImGui::BeginTabBar("outliner_tabs")) {
                if (ImGui::BeginTabItem("Actors")) {
                    editor_ui::actor_list(snap.manifest);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Models")) {
                    editor_ui::models_area(snap.manifest);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Scenes")) {
                    editor_ui::scenes_area(snap.manifest);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Quests")) {
                    editor_ui::quests_area(snap.manifest);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Story", nullptr, S.story_tab ? ImGuiTabItemFlags_SetSelected : 0)) {
                    editor_ui::story_area(snap.manifest);
                    ImGui::EndTabItem();
                }
                S.story_tab = false;
                if (ImGui::BeginTabItem("Warp")) {
                    editor_ui::warp_area();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
    if (S.inspector) {
        ImGui::SetNextWindowPos(ImVec2(w - 388, 110), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, h * 0.7f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.9f);
        if (S.focus_inspector) ImGui::SetNextWindowFocus(), S.focus_inspector = false;
        panel();
        if (ImGui::Begin("Inspector", &S.inspector)) editor_ui::actor_inspector(snap.manifest);
        ImGui::End();
    }
    if (S.problems) {
        ImGui::SetNextWindowPos(ImVec2(320, h - 330), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);
        if (S.focus_problems) ImGui::SetNextWindowFocus(), S.focus_problems = false;
        panel();
        if (ImGui::Begin("Problems & export", &S.problems)) {
            editor_ui::problems_area();
            ImGui::Separator();
            editor_ui::export_area();
        }
        ImGui::End();
    }
    if (S.test) {
        ImGui::SetNextWindowPos(ImVec2(320, 140), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
        panel();
        if (ImGui::Begin("Camera & test", &S.test)) editor_ui::camera_test_area();
        ImGui::End();
    }
    if (S.log) {
        ImGui::SetNextWindowPos(ImVec2(320, 140), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(520, 300), ImGuiCond_FirstUseEver);
        panel();
        if (ImGui::Begin("Log", &S.log)) editor_ui::log_area();
        ImGui::End();
    }
    // the quest editor: the graph is where quests are made
    if (S.quests) {
        ImGui::SetNextWindowPos(ImVec2(316, 110), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(w - 330, 1100.0f), std::min(h * 0.56f, 600.0f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.95f);
        if (S.focus_quests) ImGui::SetNextWindowFocus(), S.focus_quests = false;
        panel();
        if (ImGui::Begin("Quests", &S.quests)) editor_ui::quest_editor(snap.manifest);
        S.quests_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::End();
    }
    // the scene editor: steps, timeline, inspector
    if (S.scenes) {
        ImGui::SetNextWindowPos(ImVec2(316, h * 0.45f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::min(w - 330, 1200.0f), std::min(h * 0.5f, 520.0f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.95f);
        if (S.focus_scenes) ImGui::SetNextWindowFocus(), S.focus_scenes = false;
        panel();
        if (ImGui::Begin("Scenes", &S.scenes)) editor_ui::scene_editor(snap.manifest);
        ImGui::End();
    }
    if (S.flags) {
        // below the Quests window, at the right
        const float top = 110 + std::min(h * 0.56f, 600.0f) + 8;
        ImGui::SetNextWindowPos(ImVec2(w - 456, top), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(440, std::max(140.0f, h - top - 70)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.92f);
        panel();
        if (ImGui::Begin("Flags", &S.flags)) editor_ui::flags_area();
        ImGui::End();
    }
    if (S.search) {
        ImGui::SetNextWindowPos(ImVec2(320, 110), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
        panel();
        if (ImGui::Begin("Search", &S.search)) editor_ui::search_area();
        ImGui::End();
    }
}

// the panels' places and sizes, remembered between sessions (the host's settings, never next to the game)
void layout(ImGuiIO& io) {
    if (!S.layout_loaded) {
        S.layout_loaded = true;
        std::string saved;
        if (hostui::get("editorPanels", saved) && !saved.empty()) {
            for (auto& ch : saved)
                if (ch == '|') ch = '\n';
            ImGui::LoadIniSettingsFromMemory(saved.c_str(), saved.size());
        }
    }
    if (io.WantSaveIniSettings) {
        io.WantSaveIniSettings = false;
        size_t n = 0;
        std::string text = ImGui::SaveIniSettingsToMemory(&n);
        // only the editor's windows
        std::string kept, block;
        bool keep = false;
        auto flush = [&] {
            if (keep) kept += block;
            block.clear();
        };
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            const std::string line = text.substr(at, end - at);
            if (!line.empty() && line[0] == '[') {
                flush();
                keep = line == "[Window][Editor]" || line == "[Window][Outliner]" || line == "[Window][Inspector]" ||
                       line == "[Window][Problems & export]" || line == "[Window][Camera & test]" || line == "[Window][Log]" ||
                       line == "[Window][Quests]" || line == "[Window][Flags]" || line == "[Window][Search]" || line == "[Window][Quest (playing)]" ||
                       line == "[Docking][Data]";  // where the panels are docked
            }
            block += line + "|";
            at = end + 1;
        }
        flush();
        hostui::post([kept] { hostui::set("editorPanels", kept); });
    }
}

// Esc taken by the input thread (InputLayer::cancel): cancel what was going on, at the start of the frame
void escape_frame() {
    switch (g_escape.requested()) {
    case editor_escape::Cancel::Drag: cancel_drag(); break;
    case editor_escape::Cancel::Connect: editor_ui::cancel_quest_connect(); break;
    case editor_escape::Cancel::Placing: S.placing.clear(); break;
    case editor_escape::Cancel::None: break;
    }
}
void publish_escape(bool view) {
    g_escape.publish(view ? editor_escape::what(S.dragging, S.quests && editor_ui::quest_connecting(), !S.placing.empty()) : editor_escape::Cancel::None);
}

bool wanted(std::string&) { return editor::editor_view() || editor::playing() || editor::previewing() || editor_mcp::prompt_waiting(); }
bool draw(float w, float h) {
    ImGuiIO& io = ImGui::GetIO();
    editor_ui::refresh();
    escape_frame();
    editor_ui::mcp_prompt();  // a tool client's question comes first, whatever the editor shows
    if (editor::previewing()) {
        // a scene preview: only its bar over the game
        publish_escape(false);
        cancel_drag();
        ImGui::SetNextWindowPos(ImVec2(w * 0.5f, 10), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##editor_preview", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav);
        const std::string label = editor::preview_label();
        ImGui::TextColored(kGood, "%s", label.empty() ? "Scene preview (test mode)" : (label + " (test mode)").c_str());
        ImGui::SameLine();
        if (ImGui::Button("Skip")) editor_ui::command("skip_scene");
        ImGui::SameLine();
        if (ImGui::Button("Stop / back to editor")) editor_ui::command("stop_scene");
        ImGui::End();
        return false;
    }
    if (!editor::editor_view() && !editor::playing()) return publish_escape(false), false;
    if (editor::playing()) {
        publish_escape(false);
        cancel_drag();
        // only Stop over the running game
        ImGui::SetNextWindowPos(ImVec2(w * 0.5f, 10), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::Begin("##editor_play", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav);
        ImGui::TextColored(kGood, "Playing (test mode)");
        ImGui::SameLine();
        if (ImGui::Button("Stop / back to editor")) editor_ui::command("stop");
        ImGui::SameLine();
        ImGui::TextDisabled("Tab");
        if (const std::string quest = editor_ui::quest_status(); !quest.empty()) ImGui::TextColored(kHead, "%s", quest.c_str());
        ImGui::End();
        // the quest graph over the game, the step it is at highlighted live
        if (S.quests && editor_ui::quest_graph_while_playing()) {
            layout(io);
            ImGui::SetNextWindowPos(ImVec2(w - 620, 70), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(600, 300), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.85f);
            if (ImGui::Begin("Quest (playing)", nullptr, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking)) editor_ui::quest_live(editor_ui::snapshot().manifest);
            ImGui::End();
        }
        return false;
    }
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  // the launch mode only (this layer exists only there)
    layout(io);
    ImGuizmo::BeginFrame();
    world(w, h);
    dockspace(w, h);
    toolbar(w);
    panels(w, h);
    status_bar(w, h);
    publish_escape(true);
    // undo / redo / remove from the keyboard (not while typing)
    if (!io.WantTextInput) {
        const bool cmd = io.KeyCtrl || io.KeySuper;
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_Z, false)) editor_ui::command(io.KeyShift ? "redo" : "undo");
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_Y, false)) editor_ui::command("redo");
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_D, false) && !editor_ui::selected().empty()) {
            Value p;
            p["id"] = editor_ui::selected();
            editor_ui::command("duplicate_actor", p, [](const Value& r) {
                if (r.get("ok").boolean) editor_ui::selected() = r.get("result").get("id").string();
            });
        }
        // copy and paste: the selected actors (also into another room: they go where the camera looks)
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_C, false) && !editor_ui::selected().empty() && !S.quests_focused && !editor_ui::scenes_focused()) {
            Value p, ids;
            ids.type = Value::Array;
            auto group = editor_ui::multi_selection();
            if (std::find(group.begin(), group.end(), editor_ui::selected()) == group.end()) group = {editor_ui::selected()};
            for (const auto& id : group) ids.array.push_back(Value(id));
            p["ids"] = ids;
            editor_ui::command("copy", p);
        }
        if (cmd && ImGui::IsKeyPressed(ImGuiKey_V, false) && !S.quests_focused && !editor_ui::scenes_focused()) {
            const View v = view_of(io.DisplaySize.x, io.DisplaySize.y);
            if (v.ok) {
                Value prm, o, d;
                o["x"] = v.eye[0], o["y"] = v.eye[1], o["z"] = v.eye[2];
                d["x"] = -v.back[0], d["y"] = -v.back[1], d["z"] = -v.back[2];
                prm["origin"] = o;
                prm["direction"] = d;
                editor_ui::command("pick", prm, [](const Value& r) {
                    Value p;
                    if (r.get("ok").boolean) {
                        p["at"] = "position";
                        p["position"] = r.get("result").get("position");
                    }
                    editor_ui::command("paste", p, [](const Value& pr) {
                        if (!pr.get("ok").boolean || pr.get("result").get("ids").array.empty()) return;
                        auto& many = editor_ui::multi_selection();
                        many.clear();
                        for (const auto& id : pr.get("result").get("ids").array)
                            if (id.type == Value::String) many.push_back(id.text);
                        if (!many.empty()) editor_ui::selected() = many.back();
                    });
                });
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !editor_ui::selected().empty()) {
            Value p;
            p["id"] = editor_ui::selected();
            editor_ui::command("focus", p);
        }
    }
    return false;
}

// Tab (when not typing) and the controller's View button: Play / Stop
bool key(int code, bool down, bool repeat, bool typing) {
    if (code != kVK_Tab || typing || !(editor::editor_view() || editor::playing())) return false;
    if (down && !repeat) editor::toggle_play();
    return true;
}
bool view_button(const overlay::PadValues& pad, bool menu_open) {
    static bool held = false;
    const bool now = pad[input_map::kPadOptions] > 0.5f;
    const bool press = now && !held;
    held = now;
    if (menu_open || !editor::launch_mode() || !(editor::editor_view() || editor::playing())) return false;
    if (press) editor::toggle_play();
    return now;
}
}  // namespace

void register_layers() {
    overlay::ext::add_frame_layer({wanted, draw});
    overlay::ext::InputLayer layer;
    layer.mode = [] { return editor::editor_view() ? 1 : editor::playing() ? 2 : 0; };
    layer.key = key;
    layer.cancel = [] { return g_escape.take(); };
    overlay::ext::set_input_layer(std::move(layer));
    overlay::ext::add_shortcut(view_button);
    // test aid: WWHD_TEST_EDITOR_WINDOWS=problems,test,log opens more windows (screenshots; resetlayout: Reset layout)
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_WINDOWS")) {
        const std::string w = e;
        S.problems = w.find("problems") != std::string::npos;
        S.test = w.find("test") != std::string::npos;
        S.log = w.find("log") != std::string::npos;
        S.quests = w.find("quests") != std::string::npos;
        S.flags = w.find("flags") != std::string::npos;
        S.search = w.find("search") != std::string::npos;
        S.scenes = w.find("scenes") != std::string::npos;
        S.reset_layout = w.find("resetlayout") != std::string::npos;  // as Reset layout at the first frame
        if (w.find("nooutliner") != std::string::npos) S.outliner = false;
        if (w.find("noinspector") != std::string::npos) S.inspector = false;
    }
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_PLACING")) S.placing = e;
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_ACTOR")) editor_ui::selected() = e;
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_FIELD")) editor_ui::focus_field() = e;
    if (const char* e = std::getenv("WWHD_TEST_EDITOR_STORY")) {  // kind:id, the Story tab with that entry
        const std::string story = e;
        editor_ui::show_story(story.substr(0, story.find(':')), story.find(':') == std::string::npos ? std::string() : story.substr(story.find(':') + 1));
    }
}
void show_inspector() {
    S.inspector = true;
    S.focus_inspector = true;
}
void show_problems() {
    S.problems = true;
    S.focus_problems = true;
}
void show_quests() {
    S.quests = true;
    S.focus_quests = true;
}
void show_scenes() {
    S.scenes = true;
    S.focus_scenes = true;
}
void show_story() {
    S.outliner = true;
    S.story_tab = true;
}

}  // namespace editor_shell

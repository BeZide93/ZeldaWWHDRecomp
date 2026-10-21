// D-sweep proof of concept (debug, off by default): predict pipeline keys by drawing every model of the loaded
// archives once with the game's own code, instead of playing.
//
// WWHD_DSWEEP=<start step>[,<models per batch>[,<frames per batch>[,<tev type>]]] turns it on. From the given logic
// step on, the player's draw (fopAc_Draw of Link) also draws the models of every loaded archive, a few per batch:
//  - object archives: the J3DModelData of the archive resource table (dRes_info_c::loadResourceTable entries whose
//    loader is loadModel or loadMaterialModel); path archives (stage, rooms, LOD): every bfres model in pool order;
//  - mDoExt_J3DModel__create on a solid heap per batch (freed kHeaps batches later), dKy_tevstr_init,
//    settingTevStruct + setLightTevColorType, then mDoExt_modelUpdateDL (actors) or calc/clip/entry into the BG
//    lists (room models, as daBg_c::draw). The game's layers then draw the lists in every pass (shadow, pre-pass,
//    main), so the renderer creates the pipelines those models need, as in play.
//  - joint callbacks, unusable joint matrix calculators and material animation slots of the shared model data
//    belong to the owning actors; they are switched off while the sweep's instance is calculated.
// WWHD_DSWEEP_SKIP=archive:name,... leaves models out; WWHD_DSWEEP_FILTER=text keeps only matching ones;
// WWHD_DSWEEP_ONLY=<step>[:<profile>...] stops actor draws (except the listed profiles) so a run's pipelines
// belong to the sweep; WWHD_DSWEEP_TIME=step:degrees,... sets the time of day for reference runs.
// Logs: "[dsweep] archive ..." per loaded archive and "[dsweep] batch ..." per batch (models, logic step, time).
// With WWHD_VK_KEY_LOG the Vulkan renderer writes the batch number next to every pipeline it creates.
#include "dsweep.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "interp.h"
#include "runtime.h"
#include "guest_addr.h"
#include "mods/guest_heap_query.h"

namespace dsweep {
extern std::vector<uint32_t> g_bg_actors;
namespace {
std::atomic<int> g_tag{0};

struct Config {
    bool on = false;
    uint64_t start = 0;
    unsigned per = 6, hold = 3, tev = 0;
};
const Config& config() {
    static const Config value = [] {
        Config c;
        const char* e = std::getenv("WWHD_DSWEEP");
        if (!e || !*e) return c;
        unsigned long v[4] = {0, 6, 3, 0};
        char* p = const_cast<char*>(e);
        for (int i = 0; i < 4 && *p; ++i) {
            v[i] = std::strtoul(p, &p, 10);
            if (*p == ',') ++p;
        }
        c.on = true;
        c.start = v[0];
        c.per = v[1] ? unsigned(v[1]) : 1;
        c.hold = v[2] ? unsigned(v[2]) : 1;
        c.tev = unsigned(v[3]);
        return c;
    }();
    return value;
}

struct Model {
    std::string archive, name;
    uint32_t data = 0, model = 0;
    bool world = false;  // stage and room archives: their models are placed in world coordinates
    bool failed = false;
    uint32_t heap = 0;
    bool logged = false;
};
struct State {
    bool inventoried = false, done = false;
    uint32_t heap = 0, scratch = 0, parent = 0, heap_size = 0;
    static constexpr int kHeaps = 8;
    uint32_t heaps[kHeaps] = {};  // the solid heaps of the last batches; a batch's models live until kHeaps batches later
    std::vector<Model> models;
    int64_t started_ns = 0;
} g;

bool mem2(uint32_t a, uint32_t size = 4) { return a >= mem::kMem2Start && a <= mem::kMem2End - size; }
uint32_t call(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args = {}) {
    Cpu k = *c;
    return guest_call(&k, fn, args);
}
uint32_t call_fn(Cpu* c, uint32_t fn, std::initializer_list<uint32_t> args) { return call(c, fn, args); }
std::string cstring(uint32_t a, unsigned max = 64) {
    std::string s;
    if (!mem2(a, 1)) return s;
    for (unsigned i = 0; i < max; ++i) {
        const uint8_t ch = ld8(a + i);
        if (!ch) return s;
        if (ch < 0x20 || ch > 0x7E) return {};
        s.push_back(char(ch));
    }
    return {};
}
int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// dRes_info_c (HD layout, d_resorce.h ResourceArchiveInfoHD): name +0x1C, archive +0x5C, mRes +0x60, count +0x84.
// Loader method table (ResourceMethod, 8 bytes per resource type) and the archive resource table rows (hash,
// entries, count; entries are {name, type}) as dRes_info_c::loadResourceTable reads them.
constexpr uint32_t kRowCount = 629;
void inventory(Cpu* c) {
    const uint32_t control = ld32(GD(0x101F4F28));
    const uint32_t loadModel = GC(0x02608308), loadMaterialModel = GC(0x0260A0B4);
    const uint32_t rows = GD(0x1009BB1C), methods = GD(0x100E1C40);
    std::vector<uint32_t> seen;
    unsigned archives = 0;
    for (unsigned i = 0; i < 128 && mem2(control); ++i) {
        const uint32_t node = control + 0x28 + i * 0x20;
        const uint32_t info = ld32(node + 0x18);
        if (!mem2(info, 0x8C) || std::find(seen.begin(), seen.end(), info) != seen.end()) continue;
        const std::string name = cstring(info + 0x1C);
        const uint32_t archive = ld32(info + 0x5C), res = ld32(info + 0x60);
        const int32_t count = int32_t(ld32(info + 0x84));
        if (name.empty() || !mem2(archive, 0x24) || count < 0 || count > 8192 || (count && !mem2(res, count * 4))) continue;
        seen.push_back(info);
        ++archives;
        const uint32_t header = ld32(archive + 0x14);
        const unsigned fmdl = mem2(header, 0x68) ? ld16(header + 0x50) : 0;
        const uint32_t hash = call(c, GC(0x0273B264), {info + 0x1C, uint32_t(name.size())});
        uint32_t entries = 0, entryCount = 0;
        for (uint32_t r = 0; r < kRowCount; ++r)
            if (ld32(rows + r * 12) == hash) {
                entries = ld32(rows + r * 12 + 4);
                entryCount = ld32(rows + r * 12 + 8);
                break;
            }
        unsigned found = 0;
        const bool world = name.rfind("Room", 0) == 0 || name.rfind("Stage", 0) == 0;
        for (uint32_t e = 0; e < entryCount && mem2(entries, entryCount * 8); ++e) {
            const uint32_t type = ld32(entries + e * 8 + 4);
            if (type > 64) continue;
            const uint32_t m = methods + type * 8;
            const int16_t adjust = int16_t(ld16(m)), selector = int16_t(ld16(m + 2));
            uint32_t target = 0;
            if (!selector) continue;
            if (selector < 0) target = ld32(m + 4);
            else {
                const uint32_t object = info + uint32_t(int32_t(adjust));
                const uint32_t vt = ld32(object + uint32_t(int32_t(int16_t(ld16(m + 6)))));
                if (mem2(vt)) target = ld32(vt + uint32_t(selector) * 8 + 4);
            }
            if (target != loadModel && target != loadMaterialModel) continue;
            if (int32_t(e) >= count) continue;
            const uint32_t data = ld32(res + e * 4);
            if (!mem2(data)) continue;
            Model model;
            model.archive = name;
            model.name = cstring(ld32(entries + e * 8), 128);
            // debug: WWHD_DSWEEP_SKIP=archive:name,... leaves out models whose draw needs their actor
            if (const char* skip = std::getenv("WWHD_DSWEEP_SKIP"); skip && std::strstr(skip, (name + ":" + model.name).c_str())) {
                LOG("[dsweep] skipped %s:%s", name.c_str(), model.name.c_str());
                continue;
            }
            model.data = data;
            model.world = world;
            g.models.push_back(model);
            ++found;
        }
        LOG("[dsweep] archive %s info=%08X resources=%d table=%u models=%u bfres_models=%u", name.c_str(), info, count,
            entryCount, found, fmdl);
    }
    // path archives (stage and room files): dRes_info_c path variant, every bfres model created in pool order
    for (unsigned i = 0; i < 128 && mem2(control); ++i) {
        const uint32_t node = control + 0x103C + i * 0x20;
        const uint32_t info = ld32(node + 0x18);
        if (!mem2(info, 0xC8) || std::find(seen.begin(), seen.end(), info) != seen.end()) continue;
        const std::string name = cstring(info + 0x1C), path = cstring(info + 0x68);
        const uint32_t archive = ld32(info + 0xA8), storage = ld32(info + 0xB4);
        if (name.empty() || !mem2(archive, 0x24) || !mem2(storage)) continue;
        seen.push_back(info);
        ++archives;
        const uint32_t header = ld32(archive + 0x14);
        const unsigned fmdl = mem2(header, 0x68) ? ld16(header + 0x50) : 0;
        unsigned found = 0;
        for (unsigned m = 0; m < fmdl && m < 256; ++m) {
            const uint32_t data = ld32(storage + m * 0x20 + 0x18);
            if (!mem2(data)) continue;
            Model model;
            model.archive = name + "@" + path;
            model.name = "model" + std::to_string(m);
            model.data = data;
            model.world = true;
            g.models.push_back(model);
            ++found;
        }
        LOG("[dsweep] path archive %s (%s) info=%08X models=%u bfres_models=%u", name.c_str(), path.c_str(), info, found, fmdl);
    }
    // room backgrounds as daBg_c holds them (bg[4] at +0x3B4, 0x14 each: model; J3DModel+0xAC: its model data)
    for (uint32_t bg : g_bg_actors)
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t model = ld32(bg + 0x3B4 + i * 0x14);
            if (!mem2(model, 0xB0) || !mem2(ld32(model + 0xAC))) continue;
            const uint32_t data = ld32(model + 0xAC);
            bool known = false;
            for (auto& m : g.models) known |= m.data == data;
            LOG("[dsweep] Bg actor %08X room %d model%u data %08X %s", bg, int(int8_t(ld8(bg + 0x326))), i, data,
                known ? "(already listed)" : "(added)");
            if (known) continue;
            Model m;
            m.archive = "Bg" + std::to_string(int(int8_t(ld8(bg + 0x326))));
            m.name = "model" + std::to_string(i);
            m.data = data;
            m.world = true;
            g.models.push_back(m);
        }
    // debug: WWHD_DSWEEP_FILTER=text keeps only the models whose "archive:name" contains it
    if (const char* f = std::getenv("WWHD_DSWEEP_FILTER"); f && *f)
        g.models.erase(std::remove_if(g.models.begin(), g.models.end(),
                                      [f](const Model& m) { return (m.archive + ":" + m.name).find(f) == std::string::npos; }),
                       g.models.end());
    LOG("[dsweep] %u archives, %zu models", archives, g.models.size());
    // the parent heap with the most free space (archive, game, zelda, command), for one solid heap per batch
    const uint32_t getters[] = {GC(0x025E3268), GC(0x025E2FBC), GC(0x025EE048), GC(0x025EE060)};
    uint32_t best = 0;
    for (uint32_t getter : getters) {
        const uint32_t heap = call(c, getter);
        const uint32_t free = mem2(heap) ? guest_heap::free_size(c, heap, call_fn) : 0;
        LOG("[dsweep] heap %08X free %u KiB", heap, free >> 10);
        if (free > best) best = free, g.parent = heap;
    }
    g.heap_size = std::min<uint32_t>(best / (2 * State::kHeaps), 4u << 20) & ~0xFFFu;
    g.scratch = call(c, GC(0x0273AD10), {0x800});  // operator new: the light state and a position
    if (g.scratch) std::memset(mem::ptr(g.scratch), 0, 0x800);
}

void draw_model(Cpu* c, Model& m, uint32_t link) {
    if (m.failed || !g.heap || !g.scratch) return;
    if (m.model && std::find(std::begin(g.heaps), std::end(g.heaps), m.heap) == std::end(g.heaps)) m.model = 0;
    if (!m.model) {
        const uint32_t previous = call(c, GC(0x025E3570), {g.heap});  // mDoExt_setCurrentHeap
        // mDoExt_J3DModel__create with the flags the owners use: daBg_c (0, 0x11000022), actors (0x80000, 0x11000022)
        m.model = call(c, GC(0x025E38E0), {m.data, m.world ? 0u : 0x80000u, 0x11000022u});
        m.heap = g.heap;
        call(c, GC(0x025E3570), {previous});
        if (!m.model) {
            m.failed = true;
            LOG("[dsweep] create failed %s:%s", m.archive.c_str(), m.name.c_str());
            return;
        }
    }
    const float x = m.world ? 0.f : float(ldf32(link + 0x314));
    const float y = m.world ? 0.f : float(ldf32(link + 0x318)) + 60.f;
    const float z = m.world ? 0.f : float(ldf32(link + 0x31C));
    const float matrix[12] = {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z};
    for (int i = 0; i < 3; ++i) st32(m.model + 0xBC + i * 4, f32_as_u32(1.f));
    for (int i = 0; i < 12; ++i) st32(m.model + 0xC8 + i * 4, f32_as_u32(matrix[i]));
    const uint32_t position = g.scratch + 0x700, tevstr = g.scratch;
    st32(position, f32_as_u32(x));
    st32(position + 4, f32_as_u32(y));
    st32(position + 8, f32_as_u32(z));
    call(c, GC(0x0255FFF4), {tevstr, uint32_t(int32_t(int8_t(ld8(link + 0x326)))), 0xFF});  // dKy_tevstr_init (room)
    const uint32_t env = call(c, GC(0x02555D0C));                                   // dKy_getEnvlight
    // room models as daBg_c::draw sets them up: TEV_TYPE_BG0 + model index, no position; actors TEV_TYPE_ACTOR
    const uint32_t bgIndex = m.world && m.name.rfind("model", 0) == 0 ? uint32_t(std::atoi(m.name.c_str() + 5)) : 0;
    const uint32_t tevType = m.world ? 1 + bgIndex : config().tev;
    call(c, GC(0x025626A4), {env, tevType, m.world ? 0u : position, tevstr});  // settingTevStruct
    call(c, GC(0x02562F5C), {env, m.model, tevstr});                               // setLightTevColorType
    // stage and room models go into the BG lists as the game's background does (dComIfGd_setListBG)
    const uint32_t play = call(c, GC(0x025200D4)), opa = ld32(GD(0x104B4634)), xlu = ld32(GD(0x104B4638));
    if (m.world && play) {  // model 1 of a room goes into the INVISIBLE lists, as in daBg_c::draw
        st32(GD(0x104B4634), ld32(play + (bgIndex == 1 ? 0x5D8C : 0x5D70)));
        st32(GD(0x104B4638), ld32(play + (bgIndex == 1 ? 0x5D90 : 0x5D74)));
    }
    // joint callbacks belong to the actors that share this model data: off while the sweep's instance is calculated
    const uint32_t joints = ld32(m.data + 8), jointCount = std::min<uint32_t>(ld32(m.data + 4), 1024);
    std::vector<uint32_t> callbacks;
    if (mem2(joints, jointCount * 0x1C))
        for (uint32_t j = 0; j < jointCount; ++j)
            for (uint32_t field : {0x08u, 0x14u}) {  // joint callback; joint matrix calculator only if not callable
                const uint32_t value = ld32(joints + j * 0x1C + field);
                callbacks.push_back(value);
                const bool callable = field == 0x14 && mem2(value) && mem2(ld32(value)) && ld32(ld32(value) + 0x24);
                if (!callable) st32(joints + j * 0x1C + field, 0);
                if (!callable && value && !m.logged) LOG("[dsweep] %s:%s joint %u field %X value %08X off", m.archive.c_str(), m.name.c_str(), j, field, value);
            }
    m.logged = true;
    // material animation slots of the model data (+0x38..+0x48: set by the owning actor for its own draw, can be
    // stale between actor draws): off for the sweep's instance; animations change constants, not pipelines
    uint32_t animations[5];
    for (int i = 0; i < 5; ++i) animations[i] = ld32(m.data + 0x38 + i * 4), st32(m.data + 0x38 + i * 4, 0);
    if (m.world) {
        // as daBg_c::draw: the view clipper with a far plane of 100000, J3DModel::calc, clip, mDoExt_modelEntryDL
        const uint32_t clipper = GD(0x1048CFF0), far = ld32(clipper + 0x54);
        st32(clipper + 0x54, f32_as_u32(100000.f));
        call(c, GC(0x0283801C), {clipper});
        call(c, GC(0x027F4D5C), {m.model});
        call(c, GC(0x02838524), {clipper, m.model});
        call(c, GC(0x025E2E5C), {m.model});
        st32(clipper + 0x54, far);
        call(c, GC(0x0283801C), {clipper});
    } else
        call(c, GC(0x025E2DE0), {m.model, 0});  // mDoExt_modelUpdateDL
    for (int i = 0; i < 5; ++i) st32(m.data + 0x38 + i * 4, animations[i]);
    for (uint32_t j = 0; j < callbacks.size(); ++j) st32(joints + (j / 2) * 0x1C + (j % 2 ? 0x14 : 0x08), callbacks[j]);
    st32(GD(0x104B4634), opa);
    st32(GD(0x104B4638), xlu);
}
}  // namespace

int tag() { return g_tag.load(std::memory_order_relaxed); }

bool hide_actor_draw(uint32_t actor) {
    // WWHD_DSWEEP_ONLY=<step>[:<profile>:<profile>...]: the listed actor profiles still draw themselves
    static const char* e = std::getenv("WWHD_DSWEEP_ONLY");
    static uint64_t from = 0;
    static std::vector<uint32_t> keep;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        if (e && *e) {
            char* p = const_cast<char*>(e);
            from = std::strtoull(p, &p, 10);
            while (*p == ':') keep.push_back(uint32_t(std::strtoul(p + 1, &p, 0)));
        }
    }
    if (!from || interp::executed_steps() < from) return false;
    const uint32_t profile = mem2(actor, 0x10) ? ld16(actor + 0xE) : 0xFFFF;
    // debug: the profiles drawn (or hidden) in the frame the sweep starts, once
    static bool listed = false;
    static std::vector<uint32_t> seen;
    if (!listed && std::find(seen.begin(), seen.end(), profile) == seen.end()) seen.push_back(profile);
    if (!listed && g.inventoried) {
        listed = true;
        std::string names;
        for (uint32_t s : seen) names += " " + std::to_string(s);
        LOG("[dsweep] actor profiles drawing:%s", names.c_str());
    }
    return std::find(keep.begin(), keep.end(), profile) == keep.end();
}

// debug: WWHD_DSWEEP_TIME=step:degrees[,step:degrees...] sets the time of day (dSv_player_status_b_c::mTime,
// 0..360 for 24 hours) once at each logic step, for reference runs by day and night
void time_of_day(uint32_t actor) {
    static const char* e = std::getenv("WWHD_DSWEEP_TIME");
    static size_t done = 0;
    if (!e || ld16(actor + 0xE) != 0xA8) return;
    const uint64_t step = interp::executed_steps();
    const char* p = e;
    for (size_t i = 0; *p; ++i) {
        char* end;
        const unsigned long at = std::strtoul(p, &end, 10);
        if (*end != ':') return;
        const float degrees = std::strtof(end + 1, &end);
        if (i == done && step >= at) {
            const uint32_t info = ld32(GD(0x101F84DC));
            if (mem2(info, 0x48)) st32(info + 0x44, f32_as_u32(degrees));
            LOG("[dsweep] time of day %.1f at step %llu", double(degrees), (unsigned long long)step);
            ++done;
            return;
        }
        p = *end == ',' ? end + 1 : end;
    }
}

std::vector<uint32_t> g_bg_actors;  // daBg_c actors (profile 439 "Bg") seen before the inventory

void after_actor_draw(Cpu* c, uint32_t actor) {
    if (mem2(actor, 0x330)) time_of_day(actor);
    if (!g.inventoried && mem2(actor, 0x410) && ld16(actor + 0xE) == 439 &&
        std::find(g_bg_actors.begin(), g_bg_actors.end(), actor) == g_bg_actors.end())
        g_bg_actors.push_back(actor);
    const Config& cfg = config();
    if (cfg.on && mem2(actor, 0x330) && ld16(actor + 0xE) == 0xA8) {
        static unsigned draws = 0;
        if (draws++ % 600 == 0) LOG("[dsweep] player draw %u at step %llu", draws, (unsigned long long)interp::executed_steps());
    }
    if (!cfg.on || g.done || !mem2(actor, 0x330) || ld16(actor + 0xE) != 0xA8) return;
    const uint64_t step = interp::executed_steps();
    if (step < cfg.start) return;
    if (!g.inventoried) {
        g.inventoried = true;
        g.started_ns = now_ns();
        inventory(c);
    }
    const uint64_t batch = (step - cfg.start) / cfg.hold;
    const size_t first = size_t(batch) * cfg.per;
    if (first >= g.models.size()) {
        g.done = true;
        g_tag = 0;
        unsigned created = 0, failed = 0;
        for (auto& m : g.models) created += m.model != 0, failed += m.failed;
        LOG("[dsweep] done: %zu models, %u created, %u failed, %.1f s", g.models.size(), created, failed,
            double(now_ns() - g.started_ns) / 1e9);
        return;
    }
    if (g_tag != int(batch) + 1) {
        g_tag = int(batch) + 1;
        // a new solid heap for this batch; the one of two batches ago goes (its models were last drawn then)
        if (g.heaps[0]) call(c, GC(0x025E3868), {g.heaps[0]});  // mDoExt_destroySolidHeap
        std::move(std::begin(g.heaps) + 1, std::end(g.heaps), std::begin(g.heaps));
        g.heaps[State::kHeaps - 1] = g.heap = g.parent && g.heap_size ? call(c, GC(0x025E33A8), {g.heap_size, g.parent, 0x20}) : 0;
        if (!g.heap) LOG("[dsweep] no heap for batch %llu", (unsigned long long)batch + 1);
        std::string names;
        for (size_t i = first; i < g.models.size() && i < first + cfg.per; ++i)
            names += " " + g.models[i].archive + ":" + g.models[i].name;
        LOG("[dsweep] batch %llu step %llu ns %lld:%s", (unsigned long long)batch + 1, (unsigned long long)step,
            (long long)now_ns(), names.c_str());
    }
    for (size_t i = first; i < g.models.size() && i < first + cfg.per; ++i) draw_model(c, g.models[i], actor);
}
}  // namespace dsweep

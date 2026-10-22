// D-sweep (debug, off by default): prepare the pipelines of the game's models by drawing every model of every
// object archive once with the game's own code, instead of playing.
//
// WWHD_DSWEEP=<start step>[,<models per batch>[,<frames per batch>]] turns it on. From the given logic step on, the
// first actor draw (fopAc_Draw) of every logic step also draws a batch of models at the camera's look-at point:
//  1. the models of the archives loaded already (object archives through the archive resource table:
//     dRes_info_c::loadResourceTable entries whose loader is loadModel or loadMaterialModel);
//  2. with WWHD_DSWEEP_ARCHIVES=<file> (one archive name per line), every listed object archive in turn: loaded
//     with the game's own loader (dComIfG_resLoad, as actors load theirs), swept, then released (dComIfG_resDelete).
// Each model: mDoExt_J3DModel__create on a solid heap of its batch (freed some batches later, and all before its
// archive is released), dKy_tevstr_init + settingTevStruct + setLightTevColorType, mDoExt_modelUpdateDL into the
// frame's draw lists. The game's layers draw the lists in every pass (shadow, pre-pass, main), so the renderer
// creates the pipelines those models need, as in play. Joint callbacks, unusable joint matrix calculators and the
// material animation slots of the shared model data belong to the owning actors; they are off while the sweep's
// instance is calculated.
//
// Room and stage models are not swept: an instance of them made here draws nothing (the same model data draws
// through daBg_c's own instance; cause not found, see the D-sweep notes). Rooms are prepared by daBg itself.
//
// Crash handling and resuming (WWHD_DSWEEP_STATE=<dir>): progress.txt gets "model <archive>:<name>" before a model is
// first drawn, "load <archive>" before an archive is loaded and "archive-done <archive>" after it is released;
// skip.txt holds archive or archive:model names to leave out. A supervisor that finds the process gone adds the
// last model (or archive) of progress.txt to skip.txt and starts again; archives done already are not loaded again.
// Only names are written, no game data.
//
// Debug: WWHD_DSWEEP_SKIP=a:b,... (more names to skip), WWHD_DSWEEP_FILTER=text (only matching models),
// WWHD_DSWEEP_ONLY=<step>[:<profile>...] (no actor draws itself from that step on, except the listed profiles, so a
// run's pipelines belong to the sweep), WWHD_DSWEEP_TIME=step:degrees,... (time of day for reference runs).
// With WWHD_VK_KEY_LOG the Vulkan renderer writes the batch number next to every pipeline it creates.
#include "dsweep.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "interp.h"
#include "runtime.h"
#include "guest_addr.h"
#include "mods/guest_heap_query.h"

namespace dsweep {
extern std::atomic<bool> g_finished;
namespace {
std::atomic<int> g_tag{0};

struct Config {
    bool on = false;
    uint64_t start = 0;
    unsigned per = 1, hold = 2;
};
const Config& config() {
    static const Config value = [] {
        Config c;
        const char* e = std::getenv("WWHD_DSWEEP");
        if (!e || !*e) return c;
        unsigned long v[3] = {0, 1, 2};
        char* p = const_cast<char*>(e);
        for (int i = 0; i < 3 && *p; ++i) {
            v[i] = std::strtoul(p, &p, 10);
            if (*p == ',') ++p;
        }
        c.on = true;
        c.start = v[0];
        c.per = v[1] ? unsigned(v[1]) : 1;
        c.hold = v[2] ? unsigned(v[2]) : 1;
        return c;
    }();
    return value;
}

struct Model {
    std::string archive, name;
    uint32_t data = 0, model = 0, heap = 0;
    bool failed = false;
};

enum class Phase { Start, Resident, NextArchive, Loading, Sweeping, Draining, Done };
constexpr int kHeaps = 6;   // solid heaps alive at once: a batch's models live until kHeaps batches later
constexpr int kDrain = 6;   // frames between an archive's last draw and its release (frames in flight)
constexpr int kLoadFrames = 900;

struct State {
    Phase phase = Phase::Start;
    uint32_t scratch = 0, parent = 0, heap_size = 0, heap = 0;
    uint32_t heaps[kHeaps] = {};
    std::vector<Model> models;       // the work list of the current phase
    size_t cursor = 0;               // first model of the current batch
    unsigned frames = 0;             // frames the current batch (or load, or drain) has had
    int batch = 0;                   // batches so far (the key log's tag)
    std::vector<std::string> queue;  // object archives still to load
    std::string archive;             // the archive being loaded or swept
    std::set<std::string> resident, skip, done;
    unsigned swept = 0, failed = 0, archives = 0, load_failed = 0;
    int64_t started_ns = 0;
    std::string state_dir;
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
void progress(const std::string& line) {
    if (g.state_dir.empty()) return;
    if (FILE* f = std::fopen((g.state_dir + "/progress.txt").c_str(), "a")) {
        std::fprintf(f, "%s\n", line.c_str());
        std::fflush(f);
        std::fclose(f);
    }
}
std::vector<std::string> lines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') out.push_back(line);
    }
    return out;
}
bool skipped(const std::string& archive, const std::string& name) {
    return g.skip.count(archive) || g.skip.count(archive + ":" + name);
}

// dRes_info_c of an object archive (HD layout, d_resorce.h ResourceArchiveInfoHD): name +0x1C, archive +0x5C,
// mRes +0x60, count +0x84. The resource control (*101F4F28) keeps them in a pool of 128 nodes (+0x28, 0x20 each,
// the info at node+0x18).
std::vector<uint32_t> archive_infos() {
    std::vector<uint32_t> out;
    const uint32_t control = ld32(GD(0x101F4F28));
    for (unsigned i = 0; i < 128 && mem2(control); ++i) {
        const uint32_t info = ld32(control + 0x28 + i * 0x20 + 0x18);
        if (!mem2(info, 0x8C) || std::find(out.begin(), out.end(), info) != out.end()) continue;
        const uint32_t archive = ld32(info + 0x5C), res = ld32(info + 0x60);
        const int32_t count = int32_t(ld32(info + 0x84));
        if (cstring(info + 0x1C).empty() || !mem2(archive, 0x24) || count < 0 || count > 8192 || (count && !mem2(res, count * 4)))
            continue;
        out.push_back(info);
    }
    return out;
}
uint32_t find_info(const std::string& name) {
    for (uint32_t info : archive_infos())
        if (cstring(info + 0x1C) == name) return info;
    return 0;
}

// The models of an archive as dRes_info_c::loadResourceTable finds them: the archive's row of the resource table
// (hash of the name; rows {hash, entries, count}, entries {name, type}) and the loader of each type (ResourceMethod,
// 8 bytes per type); entry e is mRes[e].
void add_models(Cpu* c, uint32_t info) {
    constexpr uint32_t kRowCount = 629;
    const uint32_t loadModel = GC(0x02608308), loadMaterialModel = GC(0x0260A0B4);
    const uint32_t rows = GD(0x1009BB1C), methods = GD(0x100E1C40);
    const std::string name = cstring(info + 0x1C);
    const uint32_t res = ld32(info + 0x60);
    const int32_t count = int32_t(ld32(info + 0x84));
    const uint32_t hash = call(c, GC(0x0273B264), {info + 0x1C, uint32_t(name.size())});  // resourceHashString
    uint32_t entries = 0, entryCount = 0;
    for (uint32_t r = 0; r < kRowCount; ++r)
        if (ld32(rows + r * 12) == hash) {
            entries = ld32(rows + r * 12 + 4);
            entryCount = ld32(rows + r * 12 + 8);
            break;
        }
    static const char* filter = std::getenv("WWHD_DSWEEP_FILTER");
    unsigned found = 0;
    for (uint32_t e = 0; e < entryCount && int32_t(e) < count && mem2(entries, entryCount * 8); ++e) {
        const uint32_t type = ld32(entries + e * 8 + 4);
        if (type > 64) continue;
        const uint32_t m = methods + type * 8;
        const int16_t adjust = int16_t(ld16(m)), selector = int16_t(ld16(m + 2));
        uint32_t target = 0;
        if (!selector) continue;
        if (selector < 0) target = ld32(m + 4);
        else {
            const uint32_t vt = ld32(info + uint32_t(int32_t(adjust)) + uint32_t(int32_t(int16_t(ld16(m + 6)))));
            if (mem2(vt)) target = ld32(vt + uint32_t(selector) * 8 + 4);
        }
        if (target != loadModel && target != loadMaterialModel) continue;
        const uint32_t data = ld32(res + e * 4);
        if (!mem2(data)) continue;
        Model model;
        model.archive = name;
        model.name = cstring(ld32(entries + e * 8), 128);
        model.data = data;
        if (skipped(name, model.name) || (filter && *filter && (name + ":" + model.name).find(filter) == std::string::npos)) {
            LOG("[dsweep] skipped %s:%s", name.c_str(), model.name.c_str());
            continue;
        }
        g.models.push_back(model);
        ++found;
    }
    LOG("[dsweep] archive %s: %u models", name.c_str(), found);
}

// The parent heap with the most free space (archive, game, zelda, command); a batch heap is a small part of it, so
// a phone-sized heap only means smaller batches.
void choose_heaps(Cpu* c) {
    const uint32_t getters[] = {GC(0x025E3268), GC(0x025E2FBC), GC(0x025EE048), GC(0x025EE060)};
    uint32_t best = 0;
    for (uint32_t getter : getters) {
        const uint32_t heap = call(c, getter);
        const uint32_t free = mem2(heap) ? guest_heap::free_size(c, heap, call_fn) : 0;
        if (free > best) best = free, g.parent = heap;
    }
    g.heap_size = std::min<uint32_t>(best / (3 * kHeaps), 2u << 20) & ~0xFFFu;
    LOG("[dsweep] batch heaps of %u KiB from %08X (%u KiB free)", g.heap_size >> 10, g.parent, best >> 10);
}
void free_heaps(Cpu* c, int keep) {
    for (int i = 0; i < kHeaps - keep; ++i)
        if (g.heaps[i]) call(c, GC(0x025E3868), {g.heaps[i]}), g.heaps[i] = 0;  // mDoExt_destroySolidHeap
    std::rotate(std::begin(g.heaps), std::begin(g.heaps) + (kHeaps - keep), std::end(g.heaps));
}

void draw_model(Cpu* c, Model& m, uint32_t host) {
    if (m.failed || !g.heap || !g.scratch) return;
    if (m.model && std::find(std::begin(g.heaps), std::end(g.heaps), m.heap) == std::end(g.heaps)) m.model = 0;
    if (!m.model) {
        progress("model " + m.archive + ":" + m.name);
        const uint32_t previous = call(c, GC(0x025E3570), {g.heap});  // mDoExt_setCurrentHeap
        m.model = call(c, GC(0x025E38E0), {m.data, 0x80000u, 0x11000022u});  // mDoExt_J3DModel__create, as actors
        m.heap = g.heap;
        call(c, GC(0x025E3570), {previous});
        if (!m.model) {
            m.failed = true;
            ++g.failed;
            LOG("[dsweep] create failed %s:%s (batch heap %u KiB)", m.archive.c_str(), m.name.c_str(), g.heap_size >> 10);
            return;
        }
        ++g.swept;
    }
    // at the camera's look-at point (dCamera_c center +0x10), so the model is in view in any scene
    const uint32_t camera = call(c, GC(0x024F8044));
    const uint32_t at = mem2(camera, 0x20) ? camera + 0x10 : host + 0x314;
    const float x = float(ldf32(at)), y = float(ldf32(at + 4)), z = float(ldf32(at + 8));
    const float matrix[12] = {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z};
    for (int i = 0; i < 3; ++i) st32(m.model + 0xBC + i * 4, f32_as_u32(1.f));
    for (int i = 0; i < 12; ++i) st32(m.model + 0xC8 + i * 4, f32_as_u32(matrix[i]));
    const uint32_t tevstr = g.scratch, position = g.scratch + 0x700;
    st32(position, f32_as_u32(x));
    st32(position + 4, f32_as_u32(y));
    st32(position + 8, f32_as_u32(z));
    call(c, GC(0x0255FFF4), {tevstr, uint32_t(int32_t(int8_t(ld8(host + 0x326)))), 0xFF});  // dKy_tevstr_init
    const uint32_t env = call(c, GC(0x02555D0C));                                         // dKy_getEnvlight
    call(c, GC(0x025626A4), {env, 0, position, tevstr});  // settingTevStruct (TEV_TYPE_ACTOR)
    call(c, GC(0x02562F5C), {env, m.model, tevstr});      // setLightTevColorType
    // joint callbacks of the shared model data belong to the owning actors; unusable matrix calculators too
    const uint32_t joints = ld32(m.data + 8), jointCount = std::min<uint32_t>(ld32(m.data + 4), 1024);
    std::vector<uint32_t> saved;
    if (mem2(joints, jointCount * 0x1C))
        for (uint32_t j = 0; j < jointCount; ++j)
            for (uint32_t field : {0x08u, 0x14u}) {
                const uint32_t value = ld32(joints + j * 0x1C + field);
                saved.push_back(value);
                const bool callable = field == 0x14 && mem2(value) && mem2(ld32(value)) && ld32(ld32(value) + 0x24);
                if (!callable) st32(joints + j * 0x1C + field, 0);
            }
    // material animation slots of the model data (+0x38..+0x48), set by the owning actor for its own draw
    uint32_t animations[5];
    for (int i = 0; i < 5; ++i) animations[i] = ld32(m.data + 0x38 + i * 4), st32(m.data + 0x38 + i * 4, 0);
    const uint32_t opa = ld32(GD(0x104B4634)), xlu = ld32(GD(0x104B4638));
    call(c, GC(0x025E2DE0), {m.model, 0});  // mDoExt_modelUpdateDL
    st32(GD(0x104B4634), opa);
    st32(GD(0x104B4638), xlu);
    for (int i = 0; i < 5; ++i) st32(m.data + 0x38 + i * 4, animations[i]);
    for (uint32_t j = 0; j < saved.size(); ++j) st32(joints + (j / 2) * 0x1C + (j % 2 ? 0x14 : 0x08), saved[j]);
}

// One frame of the work list: true while models are left
bool sweep_frame(Cpu* c, uint32_t host) {
    const Config& cfg = config();
    if (g.frames >= cfg.hold || g.batch == 0 || g_tag == 0) {
        if (g.batch && g_tag) g.cursor += cfg.per;
        if (g.cursor >= g.models.size()) return false;
        free_heaps(c, kHeaps - 1);  // the oldest batch heap goes
        g.heaps[kHeaps - 1] = g.heap = g.parent && g.heap_size ? call(c, GC(0x025E33A8), {g.heap_size, g.parent, 0x20}) : 0;
        if (!g.heap) LOG("[dsweep] no heap for batch %d", g.batch + 1);
        g_tag = ++g.batch;
        g.frames = 0;
        std::string names;
        for (size_t i = g.cursor; i < g.models.size() && i < g.cursor + cfg.per; ++i)
            names += " " + g.models[i].archive + ":" + g.models[i].name;
        LOG("[dsweep] batch %d step %llu ns %lld:%s", g.batch, (unsigned long long)interp::executed_steps(),
            (long long)now_ns(), names.c_str());
    }
    for (size_t i = g.cursor; i < g.models.size() && i < g.cursor + cfg.per; ++i) draw_model(c, g.models[i], host);
    ++g.frames;
    return true;
}

void begin(Cpu* c) {
    g.started_ns = now_ns();
    if (const char* d = std::getenv("WWHD_DSWEEP_STATE")) g.state_dir = d;
    progress("start");
    if (const char* s = std::getenv("WWHD_DSWEEP_SKIP")) {
        std::string all = s;
        for (size_t at = 0; at <= all.size();) {
            const size_t comma = std::min(all.find(',', at), all.size());
            if (comma > at) g.skip.insert(all.substr(at, comma - at));
            at = comma + 1;
        }
    }
    bool resident_done = false;
    if (!g.state_dir.empty()) {
        for (auto& s : lines(g.state_dir + "/skip.txt")) g.skip.insert(s);
        for (auto& l : lines(g.state_dir + "/progress.txt")) {
            if (l.rfind("archive-done ", 0) == 0) g.done.insert(l.substr(13));
            if (l == "resident-done") resident_done = true;
        }
    }
    if (const char* list = std::getenv("WWHD_DSWEEP_ARCHIVES"))
        for (auto& name : lines(list))
            if (!g.done.count(name) && !g.skip.count(name)) g.queue.push_back(name);
    std::reverse(g.queue.begin(), g.queue.end());  // popped from the back, in file order
    g.scratch = mem::host_alloc(0x800, 32);  // light state, position, archive name and load phase (host-only memory)
    std::memset(mem::ptr(g.scratch), 0, 0x800);
    choose_heaps(c);
    for (uint32_t info : archive_infos()) {
        g.resident.insert(cstring(info + 0x1C));
        if (!resident_done) add_models(c, info);
    }
    LOG("[dsweep] start: %zu resident archives (%zu models%s), %zu archives queued, %zu names skipped", g.resident.size(),
        g.models.size(), resident_done ? ", done before" : "", g.queue.size(), g.skip.size());
    g.phase = Phase::Resident;
}

void finish() {
    g.phase = Phase::Done;
    g_tag = 0;
    progress("sweep-done");
    g_finished = true;
    LOG("[dsweep] done: %u models swept, %u create failures, %u archives loaded, %u load failures, %.1f s", g.swept,
        g.failed, g.archives, g.load_failed, double(now_ns() - g.started_ns) / 1e9);
}

void step(Cpu* c, uint32_t host) {
    const uint32_t name = g.scratch + 0x780, phase = g.scratch + 0x7F0;
    switch (g.phase) {
    case Phase::Start:
        begin(c);
        return step(c, host);
    case Phase::Resident:
        if (sweep_frame(c, host)) return;
        progress("resident-done");
        g.phase = Phase::Draining;
        g.archive.clear();
        g.frames = 0;
        return;
    case Phase::NextArchive:
        while (!g.queue.empty() && (g.resident.count(g.queue.back()) || g.queue.back().size() >= 0x40)) g.queue.pop_back();
        if (g.queue.empty()) return finish();
        g.archive = g.queue.back();
        g.queue.pop_back();
        std::memset(mem::ptr(name), 0, 0x78);  // the name and the load phase
        std::memcpy(mem::ptr(name), g.archive.c_str(), g.archive.size());
        progress("load " + g.archive);
        g.phase = Phase::Loading;
        g.frames = 0;
        [[fallthrough]];
    case Phase::Loading: {
        const uint32_t result = call(c, GC(0x02520460), {phase, name});  // dComIfG_resLoad (cPhs: 4 complete)
        if (result == 4) {
            ++g.archives;
            g.models.clear();
            g.cursor = 0;
            g_tag = 0;
            if (const uint32_t info = find_info(g.archive)) add_models(c, info);
            g.phase = Phase::Sweeping;
            return;
        }
        if (result >= 5 || ++g.frames > kLoadFrames) {
            ++g.load_failed;
            LOG("[dsweep] load failed %s (phase %u after %u frames)", g.archive.c_str(), result, g.frames);
            progress("load-failed " + g.archive);
            call(c, GC(0x025204C8), {phase, name});  // dComIfG_resDelete
            g.phase = Phase::NextArchive;
        }
        return;
    }
    case Phase::Sweeping:
        if (sweep_frame(c, host)) return;
        g.phase = Phase::Draining;
        g.frames = 0;
        g_tag = 0;
        return;
    case Phase::Draining:
        if (++g.frames < kDrain) return;
        free_heaps(c, 0);  // every instance goes before its archive does
        g.models.clear();
        if (!g.archive.empty()) {
            call(c, GC(0x025204C8), {phase, name});  // dComIfG_resDelete
            progress("archive-done " + g.archive);
        }
        g.phase = Phase::NextArchive;
        return;
    case Phase::Done:
        return;
    }
}
}  // namespace

int tag() { return g_tag.load(std::memory_order_relaxed); }
std::atomic<bool> g_finished{false};
bool exit_requested() { return g_finished && std::getenv("WWHD_DSWEEP_EXIT"); }

bool hide_actor_draw(uint32_t actor) {
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

void after_actor_draw(Cpu* c, uint32_t actor) {
    if (!mem2(actor, 0x330)) return;
    time_of_day(actor);
    const Config& cfg = config();
    const uint64_t now = interp::executed_steps();
    if (!cfg.on || now < cfg.start) return;
    // once per logic step, from the first actor drawn (the camera and the frame's draw lists are set up then)
    static uint64_t last = ~0ull;
    if (now == last) return;
    last = now;
    step(c, actor);
}
}  // namespace dsweep

// Placed residents walking a mod route (story_resident_route.h, story_resident_routes.cpp) with
// synthetic guest memory and no game: the record, the route hand-over at creation, the pass test, the
// authored speed, once, pauses, the turning fallback and the stuck and fall guards. The game's own
// walk step is a stand-in that walks the record the way the game does (pass test, next point, the turn
// at the end of a route without the loop flag).
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "mods/story_resident_route.h"
#include "mods/story_resident_routes.h"
#include "guest_addr.h"
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

std::vector<std::string> logged;
void log_msg(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    logged.emplace_back(line);
}

namespace {
namespace route = story_resident_route;
namespace routes = story_resident_routes;
constexpr uint32_t kArena = 0x20000000, kRecords = 0x60100000, kActor = kArena + 0x4000, kOther = kArena + 0x8000, kDat = kArena + 0xC000, kRetailRoute = kArena + 0xE000;
void map_guest(uint32_t at, uint32_t size) {
#ifdef _WIN32
    if (!VirtualAlloc(mem::ptr(at), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
        assert(VirtualFree(mem::ptr(at), size, MEM_DECOMMIT));
        assert(VirtualAlloc(mem::ptr(at), size, MEM_COMMIT, PAGE_READWRITE));
    }
#else
    assert(mmap(mem::ptr(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED);
#endif
}
float f(uint32_t a) { return u32_as_f32(ld32(a)); }
void sf(uint32_t a, float v) { st32(a, f32_as_u32(v)); }
std::map<uint32_t, void (*)(Cpu*)> hooks;
std::map<uint32_t, unsigned> originals;
std::vector<std::pair<uint32_t, uint32_t>> set_modes;
uint32_t retail_route_calls = 0;

// the actor's walk: the game's executeWalk in short, on our stand-in fields
void native_walk(uint32_t a) {
    const uint32_t run = a + 0x870;
    if (ld8(a + 0x903) != 2 || !ld32(run)) return;
    Cpu pass{};
    pass.r[3] = run;
    pass.r[4] = a + 0x314;
    pass.r[5] = ld8(run + 6) != 0;
    hooks.at(routes::passed_function())(&pass);
    bool end = false;
    if (pass.r[3]) {
        const uint32_t path = ld32(run);
        const unsigned count = ld16(path), index = ld8(run + 5);
        const bool forward = ld8(run + 6), loop = ld8(path + 5) & 1;
        if (forward) {
            if (index + 1 < count) st8(run + 5, uint8_t(index + 1));
            else if (loop) st8(run + 5, 0);
            else st8(run + 5, uint8_t(count - 2)), end = true;
        } else {
            if (index > 0) st8(run + 5, uint8_t(index - 1));
            else if (loop) st8(run + 5, uint8_t(count - 1));
            else st8(run + 5, 1), end = true;
        }
    }
    if (const uint16_t t = ld16(a + 0x8E2)) {  // its walk timer: at 0 a strolling pause
        st16(a + 0x8E2, uint16_t(t - 1));
        if (t == 1) {
            st8(a + 0x903, 0);
            return;
        }
    }
    if (end) {
        st8(run + 6, ld8(run + 6) ^ 1);
        st8(a + 0x903, 0);
        return;
    }
    sf(a + 0x8B4, f(kDat + 0x48));  // the character's own pace each step
}
void original(Cpu* c, uint32_t fn) {
    ++originals[fn];
    if (fn == routes::set_route_function()) {  // the game's setInf: a room route by number
        ++retail_route_calls;
        st8(c->r[3] + 6, uint8_t(c->r[6]));
        st32(c->r[3], c->r[4] == 3 ? kRetailRoute : 0);
        st8(c->r[3] + 5, 0);
        c->r[3] = 1;
    } else if (fn == routes::passed_function()) {
        c->r[3] = 0;  // the retail test: never in these fixtures
    } else if (fn == routes::walk_function()) {
        native_walk(c->r[3]);
    }
}
uint32_t call(Cpu*, uint32_t fn, std::initializer_list<uint32_t> args) {
    const auto* a = args.begin();
    if (fn == GC(0x022C06F4)) {  // executeSetMode
        set_modes.push_back({a[0], a[1]});
        st8(a[0] + 0x903, uint8_t(a[1]));
        sf(a[0] + 0x8B4, 0);
    }
    return 0;
}
void actor(uint32_t a, uint32_t pid, uint8_t subtype) {
    std::memset(mem::ptr(a), 0, 0x920);
    st32(a + 4, pid);
    st16(a + 0xE, 0x175);
    st8(a + 0x905, subtype);
    st32(a + 0x8A0, kDat);
}
void place(uint32_t a, float x, float y, float z) {
    for (uint32_t at : {0x2ECu, 0x300u, 0x314u}) sf(a + at, x), sf(a + at + 4, y), sf(a + at + 8, z);
}
// one logic step: the walk step, then the move at the speed it chased (the game's posMoveF in short)
void step(uint32_t a) {
    Cpu c{};
    c.r[3] = a;
    if (ld8(a + 0x903) == 2) hooks.at(routes::walk_function())(&c);
    else if (ld8(a + 0x903) == 3) {
        hooks.at(routes::turn_function())(&c);
        st8(a + 0x903, 2);  // turned
    }
    if (ld8(a + 0x903) != 2 || !ld32(a + 0x870)) return;
    const float speed = f(a + 0x8B4);
    const uint32_t path = ld32(a + 0x870), points = ld32(path + 8), target = points + ld8(a + 0x875) * 16;
    const float dx = f(target + 4) - f(a + 0x314), dz = f(target + 12) - f(a + 0x31C), len = std::hypot(dx, dz);
    if (len > 1e-3f) sf(a + 0x314, f(a + 0x314) + dx / len * std::min(speed, len)), sf(a + 0x31C, f(a + 0x31C) + dz / len * std::min(speed, len));
}
story_notebook::ManifestPath path_of(std::initializer_list<story_notebook::WorldPoint> points, const char* mode = "once") {
    story_notebook::ManifestPath p;
    p.points = points;
    p.loop = std::string(mode) == "loop";
    p.back_and_forth = std::string(mode) == "back_and_forth";
    p.speed_given = false;
    p.speed = 0;
    return p;
}
// the creation's route lookup of an owned walker: the record
void create(uint32_t a, unsigned slot) {
    Cpu c{};
    c.r[3] = a + 0x870;
    c.r[4] = route::kRouteMarker;
    c.r[5] = 11;
    c.r[6] = 1;
    hooks.at(routes::set_route_function())(&c);
    assert(c.r[3] == 1 && ld32(a + 0x870) == routes::route_address(slot) && ld8(a + 0x875) == 0 && ld8(a + 0x876) == 1);
    st8(a + 0x875, 1);  // the game's createInit: it stands at point 0 and heads for point 1
    st8(a + 0x903, 3);
}
}  // namespace

int main() {
    map_guest(kArena, 0x20000);
    map_guest(kRecords, route::kMemoryBytes);
    sf(kDat + 0x48, 1.0f);  // its own pace: one unit a step
    st16(kDat + 0x54, 200), st16(kDat + 0x56, 300);

    // the record: the game's route header and points, the position first
    {
        const auto p = path_of({{100, 0, 0}, {100, 0, 100}}, "loop");
        const auto b = route::encode(p, {0, 5, 0}, 0x1000);
        assert(route::get32(b.data(), route::kMagicAt) == route::kMagic);
        assert(b[0] == 0 && b[1] == 3 && b[2] == 0xFF && b[3] == 0xFF && b[4] == 0xFF && b[5] == 1 && b[6] == 0xFF);
        assert(route::get32(b.data(), 8) == 0x1000 + route::kPoints);
        for (unsigned i = 0; i < 3; i++) assert(route::get32(b.data(), route::kPoints + i * 16) == 0xFFFFFFFFu);
        float x;
        uint32_t u = route::get32(b.data(), route::kPoints + 16 + 4);
        std::memcpy(&x, &u, 4);
        assert(x == 100);
        assert(b[route::kConfig] == route::kLoop);
        auto q = p;
        q.points[1].x = 101;
        assert(route::route_hash(q, {0, 5, 0}) != route::route_hash(p, {0, 5, 0}));
        q = p;
        q.pauses = true;
        assert(route::route_hash(q, {0, 5, 0}) != route::route_hash(p, {0, 5, 0}));
        q = p;
        q.speed_given = true, q.speed = 45;
        const auto c = route::encode(q, {0, 5, 0}, 0x1000);
        u = route::get32(c.data(), route::kSpeed);
        std::memcpy(&x, &u, 4);
        assert(std::fabs(x - 1.5f) < 1e-6);  // 45 units a second at 30 steps a second
    }
    // the pass test: along the way it walks, not across its neighbours
    assert(!route::passed(0, 0, 100, 0, 90, 30, 4) && route::passed(0, 0, 100, 0, 101, 30, 4) && route::passed(0, 0, 100, 0, 97, 2, 4));
    // a sharp corner: past the corner point's line of the game's test, not past this one yet
    assert(!route::passed(820, -199100, 700, -199250, 750, -199150, 4));
    assert(route::previous_point(1, 3, true, false) == 0 && route::previous_point(0, 3, true, true) == 2 && route::previous_point(0, 3, false, false) == 1 && route::previous_point(2, 3, false, true) == 0);
    {
        const auto n = route::next_point(2, 3, true, false);
        assert(n.index == 1 && !n.forward && n.end);
        const auto l = route::next_point(2, 3, true, true);
        assert(l.index == 0 && l.forward && !l.end);
    }
    // the guards in short: no progress for 45 steps: a skip; three in a row: stop; far below: back
    {
        route::Guard g;
        g.anchor[0] = 0, g.anchor[1] = 0;
        route::Act act = route::Act::None;
        for (int n = 0; n < 44; n++) assert(route::guard_step(g, 1, 0, 1, true, false, 0) == route::Act::None);
        act = route::guard_step(g, 1, 0, 1, true, false, 0);
        assert(act == route::Act::NextPoint && g.status == route::Status::Skipped && g.skips == 1);
        for (int n = 0; n < 45; n++) act = route::guard_step(g, 1, 0, 1, true, false, 0);
        assert(act == route::Act::NextPoint && g.skips == 2);
        for (int n = 0; n < 45; n++) act = route::guard_step(g, 1, 0, 1, true, false, 0);
        assert(act == route::Act::Stop && g.status == route::Status::Stuck);
        route::Guard h;
        for (int n = 0; n < 200; n++) assert(route::guard_step(h, float(n * 2), 0, 0, true, false, 0) == route::Act::None);  // walking: no skips
        for (int n = 0; n < 100; n++) assert(route::guard_step(h, 0, 0, 0, false, false, 0) == route::Act::None);       // held (Link, a talk): no skips
        assert(route::guard_step(h, 5, -40, 5, true, false, 0) == route::Act::None && h.good[1] == -40);
        assert(route::guard_step(h, 6, -80, 6, true, false, 0) == route::Act::None && h.good[1] == -40);  // not good, not fallen
        assert(route::guard_step(h, 7, -101, 7, true, false, 0) == route::Act::ReturnToGood && h.status == route::Status::Fell && h.good[0] == 5);
        uint32_t seed = 99;
        for (int n = 0; n < 50; n++) {
            const auto t = route::pause_walk(seed);
            assert(t >= 200 && t <= 300);
        }
    }

    // the runtime: records in guest memory, the hooks on stand-in actors
    routes::set_memory(kRecords);
    routes::configure(original, call);
    assert(routes::install([](uint32_t) { return std::string(); }, [](uint32_t a, void (*h)(Cpu*)) { hooks[a] = h; }));
    assert(hooks.size() == 4);
    // another mod replacing one of them: nothing installed (fresh module state is not available here;
    // the refusal is the same check)

    // a retail route number is untouched, also for an owned walker; the marker without a record is too
    actor(kOther, 77, 9);
    {
        Cpu c{};
        c.r[3] = kOther + 0x870, c.r[4] = 3, c.r[6] = 1;
        hooks.at(routes::set_route_function())(&c);
        assert(retail_route_calls == 1 && ld32(kOther + 0x870) == kRetailRoute);
        c.r[3] = kOther + 0x870, c.r[4] = route::kRouteMarker;
        hooks.at(routes::set_route_function())(&c);
        assert(retail_route_calls == 2 && ld32(kOther + 0x870) == 0);
    }

    // a loop at its own pace: start (0,0,0), (200,0,0), (200,0,200)
    actor(kActor, 501, 1);
    place(kActor, 0, 0, 0);
    assert(routes::write(0, path_of({{200, 0, 0}, {200, 0, 200}}, "loop"), {0, 0, 0}, false));
    routes::set_owner(0, 501);
    assert(routes::slot_of(kActor) == 0 && routes::slot_of(kOther) == -1 && routes::owner(0) == 501);
    create(kActor, 0);
    std::vector<unsigned> visited;
    for (int n = 0; n < 1400; n++) {
        step(kActor);
        if (visited.empty() || visited.back() != ld8(kActor + 0x875)) visited.push_back(ld8(kActor + 0x875));
    }
    assert(visited.size() >= 6 && visited[0] == 1 && visited[1] == 2 && visited[2] == 0 && visited[3] == 1);
    assert(routes::view(0).status == route::Status::Walking && ld32(kActor + 0x870));
    // no pauses: its walk timer never runs
    assert(ld16(kActor + 0x8E2) == 0);

    // a sharp triangle never spins on the spot: each point is reached in turn
    actor(kActor, 502, 1);
    place(kActor, 600, 0, -199000);
    assert(routes::write(1, path_of({{820, 0, -199100}, {700, 0, -199250}}, "loop"), {600, 0, -199000}, false));
    routes::set_owner(1, 502);
    routes::set_owner(0, 0);
    create(kActor, 1);
    visited.clear();
    for (int n = 0; n < 1500; n++) {
        step(kActor);
        if (visited.empty() || visited.back() != ld8(kActor + 0x875)) visited.push_back(ld8(kActor + 0x875));
    }
    for (size_t i = 1; i < visited.size(); i++) assert(visited[i] == (visited[i - 1] + 1) % 3);

    // the authored speed: 45 units a second is 1.5 a step, whatever the character's own pace
    {
        auto p = path_of({{300, 0, 0}}, "back_and_forth");
        p.speed_given = true, p.speed = 45;
        actor(kActor, 503, 1);
        place(kActor, 0, 0, 0);
        assert(routes::write(2, p, {0, 0, 0}, false));
        routes::set_owner(2, 503);
        create(kActor, 2);
        step(kActor);  // the turn
        step(kActor);
        assert(std::fabs(f(kActor + 0x8B4) - 1.5f) < 1e-6);
        // back and forth: at the end it turns (the game's own turn), it is not finished
        for (int n = 0; n < 220; n++) step(kActor);
        assert(ld8(kActor + 0x876) == 0 && ld32(kActor + 0x870) && routes::view(2).status == route::Status::Walking);
    }

    // once: at the end it stands, finished; its record stays
    {
        actor(kActor, 504, 1);
        place(kActor, 0, 0, 0);
        assert(routes::write(3, path_of({{100, 0, 0}, {100, 0, 50}}), {0, 0, 0}, false));
        routes::set_owner(3, 504);
        create(kActor, 3);
        for (int n = 0; n < 400; n++) step(kActor);
        assert(!ld32(kActor + 0x870) && routes::view(3).status == route::Status::Finished && ld8(kActor + 0x876) == 1);
        assert(std::fabs(f(kActor + 0x314) - 100) < 5 && std::fabs(f(kActor + 0x31C) - 50) < 5);
        // a live edit starts it again on the new route
        assert(routes::write(3, path_of({{100, 0, 0}, {100, 0, 50}, {0, 0, 50}}), {0, 0, 0}, false));
        routes::set_owner(3, 504);
        Cpu c{};
        routes::edited(&c, kActor, 3);
        assert(ld32(kActor + 0x870) == routes::route_address(3) && ld8(kActor + 0x903) == 3 && routes::view(3).status == route::Status::Walking && routes::view(3).points == 4);
    }

    // pauses on: a character with its own rhythm keeps it; one without gets 200..300 steps
    {
        auto p = path_of({{5000, 0, 0}}, "loop");
        p.points.push_back({5000, 0, 100});
        p.pauses = true;
        actor(kActor, 505, 2);
        place(kActor, 0, 0, 0);
        assert(routes::write(4, p, {0, 0, 0}, false));
        routes::set_owner(4, 505);
        create(kActor, 4);
        st16(kDat + 0x54, 0), st16(kDat + 0x56, 0);  // no rhythm of its own
        step(kActor);
        step(kActor);
        const unsigned timer = ld16(kActor + 0x8E2);
        assert(timer >= 199 && timer <= 299);
        st16(kDat + 0x54, 200), st16(kDat + 0x56, 300);
    }

    // turning: a character whose own turning speed is 0 gets the port's
    {
        actor(kActor, 506, 9);
        assert(routes::write(5, path_of({{100, 0, 0}}), {0, 0, 0}, false));
        routes::set_owner(5, 506);
        st16(kActor + 0x8F6, 0);
        Cpu c{};
        c.r[3] = kActor;
        hooks.at(routes::turn_function())(&c);
        assert(int16_t(ld16(kActor + 0x8F6)) == route::kTurnFallback);
        st16(kActor + 0x8F6, 400);
        hooks.at(routes::turn_function())(&c);
        assert(ld16(kActor + 0x8F6) == 400);
        st16(kOther + 0x8F6, 0);
        c.r[3] = kOther;
        hooks.at(routes::turn_function())(&c);
        assert(ld16(kOther + 0x8F6) == 0);  // not ours: untouched
    }

    // the stuck guard: walking into a wall (no progress) skips points, then stops
    {
        actor(kActor, 507, 1);
        place(kActor, 0, 0, 0);
        assert(routes::write(6, path_of({{300, 0, 0}, {300, 0, 300}}, "loop"), {0, 0, 0}, false));
        routes::set_owner(6, 507);
        create(kActor, 6);
        set_modes.clear();
        step(kActor);
        for (int n = 0; n < 300; n++) {
            Cpu c{};
            c.r[3] = kActor;
            if (ld8(kActor + 0x903) == 3) st8(kActor + 0x903, 2);
            if (ld8(kActor + 0x903) == 2) hooks.at(routes::walk_function())(&c);  // walks, never moves
        }
        assert(routes::view(6).status == route::Status::Stuck && !ld32(kActor + 0x870));
        unsigned turns = 0;
        for (const auto& [a, m] : set_modes) turns += a == kActor && m == 3;
        assert(turns == 2 && set_modes.back().second == 0);
    }

    // the fall guard: far below its route it is put back where it last stood on it, and stops
    {
        actor(kActor, 508, 1);
        place(kActor, 0, 50, 0);
        assert(routes::write(7, path_of({{300, 50, 0}}, "loop"), {0, 50, 0}, false));
        routes::set_owner(7, 508);
        create(kActor, 7);
        step(kActor);
        for (int n = 0; n < 20; n++) step(kActor);
        sf(kActor + 0x318, 10);  // a step down, still on its route's ground: a good place
        const float good_x = f(kActor + 0x314);
        Cpu c{};
        c.r[3] = kActor;
        hooks.at(routes::walk_function())(&c);
        assert(routes::view(7).status == route::Status::Walking);
        sf(kActor + 0x318, -20), sf(kActor + 0x314, good_x + 20);  // dropping: not good, not fallen yet
        hooks.at(routes::walk_function())(&c);
        assert(routes::view(7).status == route::Status::Walking && ld32(kActor + 0x870));
        sf(kActor + 0x318, -51), sf(kActor + 0x314, good_x + 40);  // off the edge
        hooks.at(routes::walk_function())(&c);
        assert(routes::view(7).status == route::Status::Fell && !ld32(kActor + 0x870));
        assert(std::fabs(f(kActor + 0x314) - good_x) < 3 && f(kActor + 0x318) == 10 && f(kActor + 0x300) == f(kActor + 0x314));
    }

    // a restored record of the same route keeps its owner and guard state; another route is written
    {
        const auto p = path_of({{300, 50, 0}}, "loop");
        assert(routes::write(7, p, {0, 50, 0}, true) && routes::owner(7) == 508 && routes::view(7).status == route::Status::Fell);
        const auto q = path_of({{300, 50, 10}}, "loop");
        assert(routes::write(7, q, {0, 50, 0}, true) && routes::owner(7) == 508 && routes::view(7).status == route::Status::Walking);
        routes::clear(7);
        assert(!routes::view(7).valid && routes::owner(7) == 0);
    }
    std::puts("resident route tests passed");
    return 0;
}

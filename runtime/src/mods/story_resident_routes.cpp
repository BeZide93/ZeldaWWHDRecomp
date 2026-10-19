// SPDX-License-Identifier: MPL-2.0
#include "story_resident_routes.h"
#include "guest_addr.h"
#include "runtime.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace story_resident_routes {
namespace route = story_resident_route;
namespace {
// daNpcPeople_c (the Windfall townspeople class): its route walker and the fields the hooks use
constexpr uint16_t kPeopleProfile = 0x175;
constexpr uint32_t kPathRun = 0x870, kPathIndex = 0x875, kPathForward = 0x876, kNpcDat = 0x8A0, kTargetSpeed = 0x8B4,
                   kTimer = 0x8E2, kTurnSpeed = 0x8F6, kSeesLink = 0x8FD, kLinkClose = 0x8FE, kMode = 0x903;
constexpr uint32_t kSpeedF = 0x370, kSpeed = 0x33C, kOld = 0x300, kCurrent = 0x314;
constexpr uint8_t kWalkMode = 2, kWaitMode = 0, kTurnMode = 3;
const uint32_t kPassed = GC(0x0259E838), kSetRoute = GC(0x0259E6D0), kWalk = GC(0x022C46D0), kTurn = GC(0x022C4A04), kSetMode = GC(0x022C06F4);

uint32_t g_memory = 0;
bool g_installed = false;
void (*g_original)(Cpu*, uint32_t) = nullptr;
uint32_t (*g_call)(Cpu*, uint32_t, std::initializer_list<uint32_t>) = nullptr;

uint32_t slot_address(unsigned slot) { return g_memory + slot * route::kSlotBytes; }
bool record(unsigned slot) { return g_memory && slot < route::kSlots && ld32(slot_address(slot) + route::kMagicAt) == route::kMagic; }
float f(uint32_t a) { return u32_as_f32(ld32(a)); }
void sf(uint32_t a, float v) { st32(a, f32_as_u32(v)); }
bool people(uint32_t actor) { return actor >= mem::kMem2Start && actor < mem::kMem2End - 0x920 && !(actor & 3) && ld16(actor + 0xE) == kPeopleProfile; }
void set_mode(Cpu* c, uint32_t actor, uint32_t mode) {
    Cpu probe = *c;
    g_call(&probe, kSetMode, {actor, mode});
}
route::Guard read_guard(uint32_t rec) {
    route::Guard g;
    for (int i = 0; i < 3; i++) g.good[i] = f(rec + route::kGood + 4 * i);
    g.anchor[0] = f(rec + route::kAnchor), g.anchor[1] = f(rec + route::kAnchor + 4);
    g.stuck = ld16(rec + route::kStuck), g.skips = ld8(rec + route::kSkips), g.status = route::Status(ld8(rec + route::kStatus)), g.blocked = ld8(rec + route::kBlocked), g.in_row = ld8(rec + route::kInRow);
    return g;
}
void write_guard(uint32_t rec, const route::Guard& g) {
    for (int i = 0; i < 3; i++) sf(rec + route::kGood + 4 * i, g.good[i]);
    sf(rec + route::kAnchor, g.anchor[0]), sf(rec + route::kAnchor + 4, g.anchor[1]);
    st16(rec + route::kStuck, g.stuck), st8(rec + route::kSkips, g.skips), st8(rec + route::kStatus, uint8_t(g.status)), st8(rec + route::kBlocked, g.blocked), st8(rec + route::kInRow, g.in_row);
}
void stop(Cpu* c, uint32_t actor) {
    st32(actor + kPathRun, 0);  // no route: it stands (its Wait counts nothing down without one)
    set_mode(c, actor, kWaitMode);
}
bool trace() { static const bool on = std::getenv("WWHD_RESIDENT_ROUTE_TRACE") != nullptr; return on; }

// dNpc_PathRun_c::setInf(this, route number, room, forward)
void set_route(Cpu* c) {
    const uint32_t run = c->r[3], number = c->r[4] & 0xFF, forward = c->r[6] & 0xFF;
    const uint32_t actor = run - kPathRun;
    if (number == route::kRouteMarker && people(actor)) {
        const int slot = slot_of(actor);
        if (slot >= 0) {
            st8(run + 6, uint8_t(forward));
            st32(run, route_address(unsigned(slot)));
            st8(run + 5, 0);
            c->r[3] = 1;
            if (trace()) LOG("[resident_route] slot %d: actor %08X pid %08X takes its route", slot, actor, ld32(actor + 4));
            return;
        }
        LOG("[resident_route] actor %08X asked for the port's route without a record", actor);
    }
    g_original(c, kSetRoute);
}

// dNpc_PathRun_c::chkPointPass(this, position, forward): an owned walker's route is tested along the
// way it walks (story_resident_route::passed)
void passed(Cpu* c) {
    const uint32_t run = c->r[3], position = c->r[4];
    const uint32_t actor = run - kPathRun;
    const int slot = people(actor) ? slot_of(actor) : -1;
    const uint32_t path = slot >= 0 ? ld32(run) : 0;
    if (!path || path != route_address(unsigned(slot))) {
        g_original(c, kPassed);
        return;
    }
    const unsigned count = ld16(path), index = ld8(run + 5);
    const bool forward = (c->r[5] & 0xFF) != 0, loop = ld8(path + 5) & 1;
    if (index >= count || count < 2) {
        c->r[3] = 0;
        return;
    }
    const uint32_t points = ld32(path + 8), target = points + index * 16, from = points + route::previous_point(index, count, forward, loop) * 16;
    c->r[3] = route::passed(f(from + 4), f(from + 12), f(target + 4), f(target + 12), f(position), f(position + 8), route::kArrival) ? 1 : 0;
}

// daNpcPeople_c::executeWalk(this)
void walk(Cpu* c) {
    const uint32_t actor = c->r[3];
    const int slot = slot_of(actor);
    if (slot < 0) {
        g_original(c, kWalk);
        return;
    }
    const uint32_t rec = slot_address(unsigned(slot));
    const uint8_t config = ld8(rec + route::kConfig);
    const uint8_t index0 = ld8(actor + kPathIndex), forward0 = ld8(actor + kPathForward);
    // the game's own walk time never runs (it is set again at points, so short segments would never
    // pause); with pauses the port keeps the stroll: walk steps left in the record, then a pause (the
    // game's own Wait, its own wait time), then a new walk time in the character's rhythm
    st16(actor + kTimer, 0);
    if ((config & route::kPauses) && !ld16(rec + route::kWalkLeft)) {
        const uint32_t dat = ld32(actor + kNpcDat);
        const uint16_t lo = dat ? ld16(dat + 0x54) : 0, hi = dat ? ld16(dat + 0x56) : 0;
        uint32_t state = ld32(rec + route::kRandom);
        st16(rec + route::kWalkLeft, lo && hi >= lo && hi < 2000 ? uint16_t(lo + route::pause_walk(state) % (hi - lo + 1)) : route::pause_walk(state));
        st32(rec + route::kRandom, state);
    }
    g_original(c, kWalk);
    const uint8_t mode = ld8(actor + kMode);
    if (trace() && mode != kWalkMode)
        LOG("[resident_route] slot %d: stops walking (mode %u, link %u/%u, at point %u)", slot, mode, ld8(actor + kSeesLink), ld8(actor + kLinkClose), ld8(actor + kPathIndex));
    if ((config & route::kPauses) && mode == kWalkMode && ld32(actor + kPathRun)) {
        const uint16_t left = uint16_t(ld16(rec + route::kWalkLeft) - 1);
        st16(rec + route::kWalkLeft, left);
        if (!left) {
            if (trace()) LOG("[resident_route] slot %d: a strolling pause", slot);
            set_mode(c, actor, kWaitMode);
            return;
        }
    }
    const float speed = f(rec + route::kSpeed);
    if (mode == kWalkMode && speed > 0) sf(actor + kTargetSpeed, speed);
    const bool once = !(config & (route::kLoop | route::kBackAndForth));
    if (once && ld8(actor + kPathForward) != forward0 && ld32(actor + kPathRun)) {
        // the game turns at the end of a route without the loop flag; once ends there
        st8(actor + kPathForward, forward0);
        st32(actor + kPathRun, 0);
        st8(rec + route::kStatus, uint8_t(route::Status::Finished));
        if (trace()) LOG("[resident_route] slot %d: finished", slot);
        return;
    }
    if (!ld32(actor + kPathRun)) return;
    if (trace()) {
        static unsigned steps[route::kSlots];
        static const unsigned every = std::getenv("WWHD_RESIDENT_ROUTE_TRACE")[0] == '2' ? 1 : 30;
        if (++steps[slot] % every == 1 || every == 1)
            LOG("[resident_route] slot %d: at %.1f,%.1f,%.1f point %u %s speed %.2f/%.2f mode %u link %u/%u timer %d", slot, f(actor + kCurrent), f(actor + kCurrent + 4), f(actor + kCurrent + 8),
                ld8(actor + kPathIndex), ld8(actor + kPathForward) ? "forward" : "back", f(actor + kSpeedF), f(actor + kTargetSpeed), mode, ld8(actor + kSeesLink), ld8(actor + kLinkClose),
                int(int16_t(ld16(actor + kTimer))));
    }
    route::Guard g = read_guard(rec);
    const bool walking = mode == kWalkMode && !ld8(actor + kSeesLink) && !ld8(actor + kLinkClose);
    const bool passed = ld8(actor + kPathIndex) != index0 || ld8(actor + kPathForward) != forward0;  // the game moved it on
    const float x = f(actor + kCurrent), y = f(actor + kCurrent + 4), z = f(actor + kCurrent + 8);
    const unsigned index = ld8(actor + kPathIndex);
    const uint32_t target = ld32(route_address(unsigned(slot)) + 8) + index * 16;
    const float distance = float(std::hypot(double(f(target + 4)) - x, double(f(target + 12)) - z));
    const auto act = route::guard_step(g, x, y, z, walking, index, distance, passed ? index0 : route::kNoPoint, f(rec + route::kLowest));
    write_guard(rec, g);
    switch (act) {
    case route::Act::None: break;
    case route::Act::NextPoint: {
        const unsigned count = ld16(route_address(unsigned(slot)));
        const auto next = route::next_point(ld8(actor + kPathIndex), count, ld8(actor + kPathForward) != 0, config & route::kLoop);
        if (next.end && once) {
            st8(rec + route::kStatus, uint8_t(route::Status::Finished));
            stop(c, actor);
            break;
        }
        st8(actor + kPathIndex, uint8_t(next.index));
        st8(actor + kPathForward, next.forward ? 1 : 0);
        set_mode(c, actor, kTurnMode);
        LOG("[resident_route] slot %d: no progress at %.0f,%.0f,%.0f: skips to point %u", slot, x, y, z, next.index);
        break;
    }
    case route::Act::Stop:
        LOG("[resident_route] slot %d: blocked three times (point %u, at %.0f,%.0f,%.0f): stops", slot, index, x, y, z);
        stop(c, actor);
        break;
    case route::Act::ReturnToGood:
        LOG("[resident_route] slot %d: fell to %.0f below its route: back to %.0f,%.0f,%.0f and stops", slot, f(rec + route::kLowest) - y, g.good[0], g.good[1], g.good[2]);
        for (uint32_t place : {kOld, kCurrent})
            for (int i = 0; i < 3; i++) sf(actor + place + 4 * i, g.good[i]);
        for (int i = 0; i < 3; i++) sf(actor + kSpeed + 4 * i, 0);
        sf(actor + kSpeedF, 0);
        stop(c, actor);
        break;
    }
}

// daNpcPeople_c::executeTurn(this)
void turn(Cpu* c) {
    const uint32_t actor = c->r[3];
    const int slot = slot_of(actor);
    if (slot >= 0 && ld16(actor + kTurnSpeed) == 0) st16(actor + kTurnSpeed, uint16_t(route::kTurnFallback));
    if (slot >= 0 && trace()) {
        static unsigned steps[route::kSlots];
        if (++steps[slot] % 30 == 1)
            LOG("[resident_route] slot %d: turning at %.0f,%.0f,%.0f yaw %04X to point %u (turn speed %d)", slot, f(actor + kCurrent), f(actor + kCurrent + 4), f(actor + kCurrent + 8),
                ld16(actor + 0x322), ld8(actor + kPathIndex), int(int16_t(ld16(actor + kTurnSpeed))));
    }
    g_original(c, kTurn);
}
}  // namespace

void set_memory(uint32_t guest) {
    g_memory = guest;
    if (guest) std::memset(mem::ptr(guest), 0, route::kMemoryBytes);
}
uint32_t memory() { return g_memory; }
void configure(void (*original)(Cpu*, uint32_t), uint32_t (*call)(Cpu*, uint32_t, std::initializer_list<uint32_t>)) {
    g_original = original;
    g_call = call;
}
bool install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace) {
    if (g_installed) return true;
    if (!g_memory || !g_original || !g_call) return false;
    for (uint32_t fn : {kPassed, kSetRoute, kWalk, kTurn})
        if (!replaced_by(fn).empty()) {
            LOG("[resident_route] %08X is replaced by %s: residents do not walk routes", fn, replaced_by(fn).c_str());
            return false;
        }
    replace(kPassed, passed);
    replace(kSetRoute, set_route);
    replace(kWalk, walk);
    replace(kTurn, turn);
    g_installed = true;
    return true;
}
bool installed() { return g_installed; }
uint32_t set_route_function() { return kSetRoute; }
uint32_t passed_function() { return kPassed; }
uint32_t walk_function() { return kWalk; }
uint32_t turn_function() { return kTurn; }

bool write(unsigned slot, const story_notebook::ManifestPath& path, const story_notebook::WorldPoint& start, bool keep_state) {
    if (!g_memory || slot >= route::kSlots || path.points.empty() || path.points.size() + 1 > route::kMaxPoints) return false;
    const uint32_t at = slot_address(slot);
    const auto bytes = route::encode(path, start, at);
    if (keep_state && record(slot) && ld32(at + route::kHash) == route::get32(bytes.data(), route::kHash)) {
        std::memcpy(mem::ptr(at), bytes.data(), route::kOwner);  // the route itself again; owner and guards stay
        return true;
    }
    const uint32_t owner = record(slot) ? ld32(at + route::kOwner) : 0;
    std::memcpy(mem::ptr(at), bytes.data(), bytes.size());
    st32(at + route::kOwner, owner);
    return true;
}
void set_owner(unsigned slot, uint32_t pid) {
    if (record(slot)) st32(slot_address(slot) + route::kOwner, pid);
}
uint32_t owner(unsigned slot) { return record(slot) ? ld32(slot_address(slot) + route::kOwner) : 0; }
void clear(unsigned slot) {
    if (g_memory && slot < route::kSlots) std::memset(mem::ptr(slot_address(slot)), 0, route::kSlotBytes);
}
uint32_t route_address(unsigned slot) { return slot_address(slot) + route::kHeader; }
int slot_of(uint32_t actor) {
    if (!g_memory || !people(actor)) return -1;
    const uint32_t pid = ld32(actor + 4);
    if (!pid || pid == 0xFFFFFFFFu) return -1;
    for (unsigned s = 0; s < route::kSlots; s++)
        if (record(s) && ld32(slot_address(s) + route::kOwner) == pid) return int(s);
    return -1;
}
void edited(Cpu* c, uint32_t actor, unsigned slot) {
    if (!record(slot) || slot_of(actor) != int(slot)) return;
    const uint32_t rec = slot_address(slot);
    const unsigned count = ld16(route_address(slot));
    if (ld8(actor + kPathIndex) >= count) st8(actor + kPathIndex, uint8_t(count - 1));
    const auto status = route::Status(ld8(rec + route::kStatus));
    if (!ld32(actor + kPathRun)) {  // it had stopped (finished, blocked, fallen): the new route starts it again
        st32(actor + kPathRun, route_address(slot));
        route::Guard g = read_guard(rec);
        g.status = route::Status::Walking, g.skips = 0, g.stuck = 0, g.blocked = route::kNoPoint, g.in_row = 0;
        g.anchor[0] = 1e9f, g.anchor[1] = 0;
        write_guard(rec, g);
        set_mode(c, actor, kTurnMode);
    } else if (ld32(actor + kPathRun) != route_address(slot)) {
        st32(actor + kPathRun, route_address(slot));
    }
    (void)status;
}
View view(unsigned slot) {
    View v;
    if (!record(slot)) return v;
    v.valid = true;
    v.status = route::Status(ld8(slot_address(slot) + route::kStatus));
    v.points = ld16(route_address(slot));
    return v;
}

}  // namespace story_resident_routes

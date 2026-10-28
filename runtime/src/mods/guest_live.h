// SPDX-License-Identifier: MPL-2.0
// Mod SDK additions for live features (QoL track M0; docs/mod-sdk-v2.md "Live options, input filters and
// touches"): pure parts, checked by guest_live_test without guest code.
//
// - Live options: a manifest option with "live": true follows changes made in Settings or the item menu's page
//   while the game runs; wwhd_config_* read the value as it is now, and wwhd_config_changed(id) returns its
//   revision (1 at start, one more per change; 0 for an option the mod doesn't have). Options without "live"
//   keep their start value (the page says the change applies at the next start).
// - Input filters: each mod may register one callback that gets the controller state of each of the game's
//   reads, after the port's own (touch controls, talks by touch), in the mods' load order, and may change it.
// - Touches on the TV picture: a mod claims rectangles (layout units: 1280 x 720 around the centre, +y up);
//   a finger landing in one goes to that mod (down, moves, up) and to nothing else (no stick, no talk); without a
//   claim at that point the port's own rules decide. The newest claim wins where claims overlap.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace guestlive {

// ---- live options: revisions per mod and option
class Revisions {
public:
    // the option exists for this mod (at load): revision 1
    void declare(const std::string& mod, const std::string& option) { revs_[mod].emplace(option, 1); }
    // a live change: one more (an unknown option is not created)
    bool bump(const std::string& mod, const std::string& option) {
        auto m = revs_.find(mod);
        if (m == revs_.end()) return false;
        auto o = m->second.find(option);
        if (o == m->second.end()) return false;
        o->second++;
        return true;
    }
    uint64_t revision(const std::string& mod, const std::string& option) const {
        auto m = revs_.find(mod);
        if (m == revs_.end()) return 0;
        auto o = m->second.find(option);
        return o == m->second.end() ? 0 : o->second;
    }

private:
    std::map<std::string, std::map<std::string, uint64_t>> revs_;
};

// ---- touches
enum Kind : uint32_t { kDown = 0, kMove = 1, kUp = 2, kCancel = 3 };
struct Event {
    uint32_t kind = kDown;
    float x = 0, y = 0;
};
struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool contains(float x, float y) const { return x1 > x0 && y1 > y0 && x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};
constexpr int kSlots = 8;  // rectangles per mod

class Touch {
public:
    // a mod's rectangle in a slot (x1 <= x0 or y1 <= y0 removes it); `order` grows with each claim call
    void claim(int mod, int slot, Rect r) {
        if (slot < 0 || slot >= kSlots || mod < 0) return;
        if (size_t(mod) >= mods_.size()) mods_.resize(size_t(mod) + 1);
        Claim& c = mods_[size_t(mod)].slots[slot];
        c.rect = r;
        c.order = ++order_;
    }
    void drop(int mod) {
        if (mod >= 0 && size_t(mod) < mods_.size()) mods_[size_t(mod)] = {};
        if (owner_ == mod) owner_ = -1;
    }
    // the mod a finger landing at (x, y) belongs to, -1 none (the newest claim containing it)
    int owner_at(float x, float y) const {
        int best = -1;
        uint64_t order = 0;
        for (size_t m = 0; m < mods_.size(); m++)
            for (const Claim& c : mods_[m].slots)
                if (c.rect.contains(x, y) && c.order > order) best = int(m), order = c.order;
        return best;
    }
    bool any() const {
        for (const ModClaims& m : mods_)
            for (const Claim& c : m.slots)
                if (c.rect.x1 > c.rect.x0 && c.rect.y1 > c.rect.y0) return true;
        return false;
    }
    // One finger change (the port's single tracked finger). Returns the mod that gets it, -1: not a mod's (the
    // port's own rules have it). A finger that went down in a mod's claim stays that mod's until it lifts.
    int route(const Event& e) {
        if (e.kind == kDown) {
            owner_ = owner_at(e.x, e.y);
            return owner_;
        }
        const int o = owner_;
        if (e.kind == kUp || e.kind == kCancel) owner_ = -1;
        return o;
    }
    int finger_owner() const { return owner_; }

private:
    struct Claim {
        Rect rect;
        uint64_t order = 0;
    };
    struct ModClaims {
        Claim slots[kSlots];
    };
    std::vector<ModClaims> mods_;
    uint64_t order_ = 0;
    int owner_ = -1;
};

}  // namespace guestlive

#pragma once
// The object archives loaded in the game right now, as pipeline recipe groups (gfx/vulkan/pipeline_recipe.h,
// group_of: FNV-1a of the archive name). The game thread publishes the set when it changes (dsweep.cpp,
// read-only scan of the resource table); the Vulkan pipeline worker compiles the recipes of a newly loaded
// archive ahead of its models' first draw. Only active once a recipe database with groups (the setup's model
// sweep) is loaded: the worker turns the scan on with want().
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace pipeline_groups {
// the group of an object archive: FNV-1a (32 bits) of its name, never 0
inline uint32_t group_of(const char* name, size_t size) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; ++i) h = (h ^ uint8_t(name[i])) * 16777619u;
    return h ? h : 1;
}
struct Shared {
    std::mutex mutex;
    std::vector<uint32_t> resident;  // sorted
    std::atomic<uint64_t> generation{0};
    std::atomic<bool> wanted{false};
    std::atomic<uint32_t> sweeping{0};
};
inline Shared& shared() { static Shared value; return value; }
// worker: grouped recipes exist, start publishing
inline void want() { shared().wanted.store(true, std::memory_order_relaxed); }
inline bool wanted() { return shared().wanted.load(std::memory_order_relaxed); }
// game thread: the current set (any order); a new generation only when it changed
inline void publish(std::vector<uint32_t> groups) {
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    if (groups == s.resident) return;
    s.resident = std::move(groups);
    s.generation.fetch_add(1, std::memory_order_release);
}
// the setup's model sweep: the group of the archive whose models are being drawn (0: none); the renderer files the
// pipelines it creates or uses meanwhile under it
inline void set_sweeping(uint32_t group) { shared().sweeping.store(group, std::memory_order_relaxed); }
inline uint32_t sweeping() { return shared().sweeping.load(std::memory_order_relaxed); }
inline uint64_t generation() { return shared().generation.load(std::memory_order_acquire); }
inline std::vector<uint32_t> resident() {
    auto& s = shared();
    std::lock_guard lock(s.mutex);
    return s.resident;
}
}  // namespace pipeline_groups

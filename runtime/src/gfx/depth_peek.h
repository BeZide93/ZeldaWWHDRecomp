#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace gfx::depth_peek {
inline uint32_t from_float(float z) {
    if (!std::isfinite(z)) return 0;
    return z >= 1.0f ? 0xFFFFFFu : uint32_t(std::clamp(z, 0.0f, 1.0f) * 16777215.0f);
}
inline int pixel(uint32_t coordinate, uint32_t extent, uint32_t guestExtent) {
    return int(std::clamp(double(int32_t(coordinate)) * extent / guestExtent, 0.0, double(extent - 1)));
}
uint64_t next_ticket();
void publish(uint64_t ticket, const std::vector<uint32_t>& destinations, const std::vector<uint32_t>& depths);
// The game reads a peek's answers one step after its request, as on the Wii U, where peekData reads
// the depth buffer on the CPU and writes the answers at once (peekz.cpp). By default the renderers
// answer a peek synchronously (they wait for the GPU to finish that small copy), and the game thread
// waits at the end of its frame (gx2 swap) until every peek of that frame has been answered.
// WWHD_PEEKZ_SYNC=0 restores the former asynchronous answers (whenever the GPU frame completes).
// Counts requests (game thread) against handled requests (render thread, in command order).
struct AnswerGate {
    std::mutex mutex;
    std::condition_variable wake;
    uint64_t issued = 0, answered = 0;
    void issue() { std::lock_guard lock(mutex); ++issued; }
    void answer() { { std::lock_guard lock(mutex); ++answered; } wake.notify_all(); }
    // true: every issued request was handled (at once or within limit); false: the limit ran out, the
    // count is resynchronised so that a lost request cannot stall later frames
    bool wait(std::chrono::milliseconds limit) {
        std::unique_lock lock(mutex);
        if (wake.wait_for(lock, limit, [&] { return answered >= issued; })) return true;
        answered = issued;
        return false;
    }
};
bool synchronous();
void issued();                 // game thread: a peek request was sent to the renderer
void answered();               // render thread: that request was handled (answered, or no answer)
void wait_answered();          // game thread, at its swap: wait until every request was handled
} // namespace gfx::depth_peek

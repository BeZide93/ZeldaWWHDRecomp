// The mouse on the TV picture for the port's own pages (tv_pointer.h).
#include "tv_pointer.h"

#include <mutex>

namespace tv_pointer {
namespace {
std::atomic<unsigned> g_wanted{0};
std::mutex g_mu;
State g_state;
}  // namespace

void set_wanted(unsigned owner, bool on) {
    const unsigned was = on ? g_wanted.fetch_or(owner) : g_wanted.fetch_and(~owner);
    if (on || !(was & owner) || (was & ~owner)) return;
    std::lock_guard lock(g_mu);
    g_state.down = false;  // no longer wanted by anyone: a held click ends
}
void set_wanted(bool on) { set_wanted(kOwnerPage, on); }
bool wanted() { return g_wanted.load(std::memory_order_relaxed) != 0; }
bool wanted(unsigned owner) { return (g_wanted.load(std::memory_order_relaxed) & owner) != 0; }
void set(bool down, float x, float y, float aspect) {
    std::lock_guard lock(g_mu);
    g_state = {down, x, y, aspect};
}
State read() {
    std::lock_guard lock(g_mu);
    return g_state;
}

}  // namespace tv_pointer

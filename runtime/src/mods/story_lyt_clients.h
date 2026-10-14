// Story UI layouts: the clients of story_lyt_screen (story_lyt_screen.h), several layouts shown with the
// game's item screen at once (the notebook's page, the Delivery Bag's extra pages). Pure code, unit-tested
// in runtime/tools/story_lyt_clients_test.cpp: which client a game file read is for, and the order the
// clients are built and drawn in.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace story_lyt_screen {

// Does the game's read of `guest` ask for Layout/<layout>.szs (any folder before it, any case)?
inline bool is_layout_file(const std::string& guest, const std::string& layout) {
    const std::string want = "layout/" + layout + ".szs";
    if (layout.empty() || guest.size() < want.size()) return false;
    const std::string tail = guest.substr(guest.size() - want.size());
    for (size_t i = 0; i < want.size(); i++)
        if (std::tolower((unsigned char)tail[i]) != std::tolower((unsigned char)want[i])) return false;
    return guest.size() == want.size() || guest[guest.size() - want.size() - 1] == '/';
}

// The clients' indices (their ids) in draw order: lower `order` first (drawn first, so below the
// others), registration order among equal ones. Built in the same order.
template <class Orders>
std::vector<int> draw_order(const Orders& orders) {
    std::vector<int> ids(orders.size());
    for (size_t i = 0; i < ids.size(); i++) ids[i] = int(i);
    std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) { return orders[size_t(a)] < orders[size_t(b)]; });
    return ids;
}

}  // namespace story_lyt_screen

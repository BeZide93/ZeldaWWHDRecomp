// story_lyt_clients_test: the clients of story_lyt_screen (story_lyt_clients.h): which client a game read
// of a layout archive is for, and the order several clients' layouts are built and drawn in.
#include <cassert>
#include <cstdio>
#include <vector>

#include "mods/story_lyt_clients.h"

using namespace story_lyt_screen;

int main() {
    // the game reads Layout/<name>.szs from some folder; the match ignores case, not the folder boundary
    assert(is_layout_file("/vol/content/Common/Layout/StoryNotebook_00.szs", "StoryNotebook_00"));
    assert(is_layout_file("layout/storynotebook_00.SZS", "StoryNotebook_00"));
    assert(is_layout_file("Layout/StoryBag_00.szs", "StoryBag_00"));
    assert(!is_layout_file("/x/Layout/XStoryBag_00.szs", "StoryBag_00"));  // another layout ending in the name
    assert(!is_layout_file("/x/MyLayout/StoryBag_00.szs", "StoryBag_00"));
    assert(!is_layout_file("/x/Layout/StoryBag_00.szs.bak", "StoryBag_00"));
    assert(!is_layout_file("Layout/StoryBag_00.szs", ""));
    assert(!is_layout_file("bag_00.szs", "StoryBag_00"));

    // draw order: lower order first, registration order among equal ones
    assert((draw_order(std::vector<int>{}) == std::vector<int>{}));
    assert((draw_order(std::vector<int>{0}) == std::vector<int>{0}));
    assert((draw_order(std::vector<int>{0, 0, 0}) == std::vector<int>{0, 1, 2}));
    // the notebook (id 0, order 0) above the bag page (id 1, order -1)
    assert((draw_order(std::vector<int>{0, -1}) == std::vector<int>{1, 0}));
    assert((draw_order(std::vector<int>{5, -1, 5, 0}) == std::vector<int>{1, 3, 0, 2}));
    std::puts("story_lyt_clients: ok");
    return 0;
}

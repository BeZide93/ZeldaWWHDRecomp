// fs tests (mod_content_fs_test, language_source_test): fs.cpp without the story layouts and the item
// icon pack. Neither is served in these tests; the game's own files are read.
#include "mods/item_icons.h"
#include "mods/story_lyt_screen.h"

namespace story_lyt_screen {
std::string archive_for(const std::string&) { return {}; }
}  // namespace story_lyt_screen
namespace item_icons {
std::string pack_for(const std::string&, const std::string&) { return {}; }
}  // namespace item_icons

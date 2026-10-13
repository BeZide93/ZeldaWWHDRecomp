// The story notebook: the port's quest journal for story mods (docs/mod-sdk-v2.md, "The story
// notebook"). Link gets it with the first letter from a mod (story_mail.h). Mods fill it through the
// wwhd_quest_* services; the player opens it from the item menu and may track one quest, whose next
// step then shows on the sea chart (story_chart.h).
//
// - The quest list, the save record and the tracked quest: story_notebook_list.h.
// - The page (drawing and navigation): story_notebook_page.h.
// - Nothing of this runs unless a loaded guest mod imports a wwhd_quest_* service.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "story_notebook_list.h"

struct Cpu;
namespace input { struct PadState; }

namespace story_notebook {

// Results of the guest services (wwhd_guest.h: WWHD_QUEST_*): 0 not started, 1..254 the step
// reached, 255 done, or an error.
inline constexpr int32_t kResultNew = 0, kResultDone = 255, kResultInvalid = -1, kResultNoRoom = -2;

// wwhd_quest_define: registers the quest (or updates its texts) for this session.
int32_t define(const std::string& mod, const std::string& mod_name, const std::string& quest, const std::string& title,
               const std::string& reward, const std::string& condition);
// wwhd_quest_step: step N reached (0: only the texts), with what to do next and where
int32_t step(const std::string& mod, const std::string& quest, uint32_t step, const std::string& next, const std::string& where);
// wwhd_quest_place / wwhd_quest_no_place: the next step's place on the sea chart
int32_t place(const std::string& mod, const std::string& quest, float x, float z);
int32_t no_place(const std::string& mod, const std::string& quest);
// wwhd_quest_known, wwhd_quest_portrait: what is known so far; the quest's portrait (package image)
int32_t known(const std::string& mod, const std::string& quest, const std::string& text);
int32_t portrait(const std::string& mod, const std::string& quest, const std::string& path, const std::string& package_dir);
int32_t done(const std::string& mod, const std::string& quest);
int32_t state(const std::string& mod, const std::string& quest);

// The quests of this session (registration order) and the rows of the current save file.
std::vector<Quest> quests();
std::vector<Row> rows_now(std::vector<Quest>& storage);

// The page. open_page() from the item menu (only when the notebook is owned); the page closes
// itself with B. pad() runs once per GamePad read (game thread), on the live sample: while the page
// is open it moves through the page, draws it and leaves the game an idle controller; buttons held
// when it closes reach the game only after they are released.
bool notebook_owned();
bool open_page();
void close_page();
bool page_open();
void pad(input::PadState& p);
void mute(input::PadState& p);  // other controller reads (the Pro Controller): idle while open, held buttons after

void note_import();  // a module imports a quest service
bool imported();
// After all guest modules are loaded (and after story_mail::install): the item menu hooks.
void install(const std::function<std::string(uint32_t)>& replaced_by, const std::function<void(uint32_t, void (*)(Cpu*))>& replace);
bool installed();
void set_game_calls(void (*original)(Cpu*, uint32_t));
void state_loaded();

// The item menu hooks, exposed for runtime/tools/story_notebook_test.cpp
namespace hooks {
void inventory_item(Cpu* c);  // hdMenuItem_inventoryItem(key)
void icon_frame(Cpu* c);      // hdMenuItem itemForSlot(this, item)
void open_child(Cpu* c);      // hdMenuMain_c::openSelectedChild(index)
void touch_select(Cpu* c);    // hdMenuItem_c::selectTouchEnter()
void set_visible(Cpu* c);     // hdMenuItem_c::setVisible(visible)
void selected_value(Cpu* c);  // hdMenuMain_c::getSelectedItemValue(receiver)
void assign_button(Cpu* c);   // game system: X/Y/R assignment(button, key)
}  // namespace hooks

}  // namespace story_notebook

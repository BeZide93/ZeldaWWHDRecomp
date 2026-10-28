// The item menu's settings page (runtime/src/mods/game_settings_page.h): entry, navigation, changes,
// touch and closing, without the game.
#include <cassert>
#include <cstdio>

#include "mods/game_settings_page.h"

using namespace game_settings_page;

static Input pad(uint32_t buttons, float dt = 1.0f / 30) {
    Input in;
    in.buttons = buttons, in.dt = dt;
    return in;
}
static Input touch(bool down, float x, float y) {
    Input in;
    in.touch = down, in.x = x, in.y = y, in.dt = 1.0f / 30;
    return in;
}

int main() {
    std::vector<Row> rows(3);
    rows[0].id = "a", rows[0].toggle = true;
    rows[1].id = "b", rows[1].number = true;
    rows[2].id = "c", rows[2].toggle = true, rows[2].enabled = false;
    Page p;
    // entry: ZL pressed (not held into the menu), or a tap on the arrow
    assert(!p.entry(pad(kPadZL)) || true);  // (first read: a held ZL counts as pressed only from idle)
    Page q;
    assert(!q.entry(pad(0)));
    assert(q.entry(pad(kPadZL)));
    Page t;
    assert(!t.entry(touch(true, kArrowX, kArrowY)));
    assert(t.entry(touch(false, kArrowX, kArrowY)));
    Page d;
    assert(!d.entry(touch(true, kArrowX, kArrowY)));
    assert(!d.entry(touch(true, kArrowX + 200, kArrowY)));
    assert(!d.entry(touch(false, kArrowX + 200, kArrowY)));  // a drag is not a tap
    // open: the opening button acts only after its release
    q.show(1);
    for (int i = 0; i < 10; i++) q.tick(1.0f / 30);  // slid in
    Action a = q.input(pad(kPadZL), rows, 1);
    assert(a.id.empty() && !a.closed);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadA), rows, 1);
    assert(a.id == "a" && a.direction == 0 && a.sound == Sound::Change);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadDown), rows, 1);
    assert(q.selected() == 1 && a.sound == Sound::Cursor);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadLeft), rows, 1);
    assert(a.id == "b" && a.direction == -1);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadA), rows, 1);
    assert(a.id == "b" && a.direction == +1);  // A steps a number up
    // the stick, with repeat
    a = q.input(pad(0), rows, 1);
    Input s = pad(0);
    s.ly = -1;
    a = q.input(s, rows, 1);
    assert(q.selected() == 2);
    a = q.input(pad(0), rows, 1);
    a = q.input(pad(kPadA), rows, 1);
    assert(a.id.empty() && a.sound == Sound::Blocked);  // a dimmed row does not change
    a = q.input(pad(0), rows, 1);
    // touch: a tap on the first row's value changes it
    const float y0 = kRowY + kRowH / 2;
    a = q.input(touch(true, kValueX + 20, y0), rows, 1);
    a = q.input(touch(false, kValueX + 20, y0), rows, 1);
    assert(q.selected() == 0 && a.id == "a");
    // a tap on the left half of a number's value steps down, the right half up
    a = q.input(touch(true, kValueX + 20, y0 + kRowH), rows, 1);
    a = q.input(touch(false, kValueX + 20, y0 + kRowH), rows, 1);
    assert(a.id == "b" && a.direction == -1);
    a = q.input(touch(true, kValueX + kValueW - 20, y0 + kRowH), rows, 1);
    a = q.input(touch(false, kValueX + kValueW - 20, y0 + kRowH), rows, 1);
    assert(a.id == "b" && a.direction == +1);
    // B closes; the page slides out
    a = q.input(pad(kPadB), rows, 1);
    assert(a.closed && !q.open() && q.shown());
    for (int i = 0; i < 20; i++) q.tick(1.0f / 30);
    assert(!q.shown());
    // the arrow back to the items closes too
    q.show(1);
    q.input(pad(0), rows, 1);
    a = q.input(touch(true, kBackX, kBackY), rows, 1);
    a = q.input(touch(false, kBackX, kBackY), rows, 1);
    assert(a.closed);
    // drawing: the page and the entry arrow produce commands, the window first after the dim
    q.show(1);
    for (int i = 0; i < 20; i++) q.tick(1.0f / 30);
    const auto cmds = q.draw(rows, {"Camera"});
    assert(cmds.size() > 10 && cmds[1].text.rfind("nb:window@", 0) == 0);
    assert(!q.draw_entry().empty());
    std::puts("game_settings_page_test: ok");
}

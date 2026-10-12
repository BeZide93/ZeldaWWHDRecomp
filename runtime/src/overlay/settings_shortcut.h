#pragma once

namespace overlay {
// Driven by the input pump, not rendered frames. Times are monotonic seconds.
class SettingsShortcut {
    double since_ = -1;
    bool latched_ = false, home_ = false;
public:
    enum Action { None, Open, Close };
    Action poll(double now, bool select, bool home, bool open) {
        Action action = None;
        if (home && !home_) action = open ? Close : Open;
        home_ = home;
        if (!select) { since_ = -1; latched_ = false; }
        else if (!latched_) {
            if (open) { action = Close; latched_ = true; }
            else {
                if (since_ < 0) since_ = now;
                if (now - since_ >= 0.5) { action = Open; latched_ = true; }
            }
        }
        return action;
    }
};
}  // namespace overlay

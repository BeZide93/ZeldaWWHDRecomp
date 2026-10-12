#include "platform/android_display_rate.h"
#include <cassert>
#include <initializer_list>
using namespace android_display_rate;
int main() {
    for (int fps : {60, 120, 240}) {
        assert(requested_hz(0, fps, false) == 60);
        assert(requested_hz(2, fps, false) == 60);
        assert(requested_hz(1, fps, false) == fps);
        for (int mode : {0, 1, 2}) assert(requested_hz(mode, fps, true) == 0);
    }
    RequestState state;
    assert(!state.changed(0, 0, 60));
    assert(state.changed(1, 0, 60));
    for (int i = 0; i < 100; ++i) assert(!state.changed(1, 0, 60));
    assert(state.changed(1, 0, 120)); // settings change while playing
    assert(state.changed(1, 0, 60));
    assert(state.changed(1, 1, 60)); // replacement may reuse the native address
    assert(!state.changed(1, 1, 60));
    assert(!state.changed(0, 1, 60)); // absent surface during backgrounding
    assert(state.changed(1, 1, 60)); // resume with same address
    assert(state.changed(2, 1, 60)); // new native window
    assert(state.changed(2, 1, 0)); // clear request / opt out
    assert(!state.changed(2, 1, 0));
}

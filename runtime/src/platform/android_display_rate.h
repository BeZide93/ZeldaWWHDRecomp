// Android surface cadence policy and request deduplication, independent of SDL/JNI for testing.
#pragma once
#include <cstdint>

namespace android_display_rate {
inline int requested_hz(int mode, int interpolationFps, bool disabled) {
    if (disabled) return 0; // zero clears a previous request and lets Android choose
    // True 60 and original 30 both fit a 60 Hz panel. Use the chosen interpolation rate rather
    // than output_fps(), which is capped to the current display and cannot raise its own cap.
    return mode == 1 && interpolationFps > 60 ? interpolationFps : 60;
}
class RequestState {
    uintptr_t window_ = 0;
    unsigned revision_ = 0;
    int hz_ = -1;
public:
    bool changed(uintptr_t window, unsigned revision, int hz) {
        if (!window) {
            window_ = 0; // even if Android reuses the address, a later surface must get its request
            return false;
        }
        if (window == window_ && revision == revision_ && hz == hz_) return false;
        window_ = window;
        revision_ = revision;
        hz_ = hz;
        return true;
    }
};
} // namespace android_display_rate

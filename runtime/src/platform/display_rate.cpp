// SDL host (Windows, Linux, Android): the TV window's display refresh rate for frame interpolation
// (interp::output_fps caps 120/240 fps to it). Polled by the SDL main loop (gfx/vulkan/backend.cpp).
// The AppKit host reports it in gfx/display.mm.
//
// Android requests a cadence matching the chosen frame-rate setting (at least 60 Hz), once per
// setting or native surface change. The system may choose another mode. Actual refresh is read
// back for interpolation's cap, never used as the request (which would trap 120 fps at 60 Hz).
#include "display_rate.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "../interp.h"
#include "../runtime.h"

#ifdef __ANDROID__
#include <jni.h>
#include <android/native_window.h>
#include <atomic>
#include <cstdlib>
#include "android_display_rate.h"
#endif

namespace display_rate {
namespace {
#ifdef __ANDROID__
// static float/void methods of WwhdActivity (the game's activity class)
jclass activity_class(JNIEnv* env) {
    jobject activity = (jobject)SDL_GetAndroidActivity();
    if (!activity) return nullptr;
    jclass cls = env->GetObjectClass(activity);
    env->DeleteLocalRef(activity);
    return cls;
}
float call_float(const char* name) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    if (!env) return 0;
    jclass cls = activity_class(env);
    if (!cls) return 0;
    float v = 0;
    if (jmethodID m = env->GetStaticMethodID(cls, name, "()F")) v = env->CallStaticFloatMethod(cls, m);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        v = 0;
    }
    env->DeleteLocalRef(cls);
    return v;
}
std::atomic<unsigned> surfaceRevision{0};
void request(SDL_Window* tv) {
    static const bool disabled = [] {
        const char* value = std::getenv("WWHD_NO_60HZ");
        return value && std::atoi(value) != 0;
    }();
    static android_display_rate::RequestState state;
    auto* window = tv ? static_cast<ANativeWindow*>(SDL_GetPointerProperty(
        SDL_GetWindowProperties(tv), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)) : nullptr;
    const int hz = android_display_rate::requested_hz(interp::mode(), interp::fps(), disabled);
    if (!state.changed(reinterpret_cast<uintptr_t>(window), surfaceRevision.load(), hz)) return;
    const int result = ANativeWindow_setFrameRate(window, static_cast<float>(hz),
        ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
    if (result == 0) LOG("[display] requested %d Hz for Android surface%s", hz, disabled ? " (system choice)" : "");
    else LOG("[display] Android frame-rate request %d Hz failed (%d); system choice retained", hz, result);
}
#endif
}  // namespace

void surface_changed() {
#ifdef __ANDROID__
    ++surfaceRevision;
#endif
}

void poll(SDL_Window* tv) {
#ifdef __ANDROID__
    request(tv);
    interp::set_display_hz((int)std::lround(call_float("displayRefreshRate")));
#else
    const SDL_DisplayID id = tv ? SDL_GetDisplayForWindow(tv) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* m = id ? SDL_GetCurrentDisplayMode(id) : nullptr;
    interp::set_display_hz(m ? (int)std::lround(m->refresh_rate) : 0);
#endif
}
}  // namespace display_rate

// SPDX-License-Identifier: MPL-2.0
#include "enhanced_touch.h"
#ifdef __ANDROID__
#include <jni.h>
#include "play_context.h"
extern "C" JNIEXPORT void JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeEnhancedSequence(JNIEnv*, jclass, jboolean active) {
    enhanced::sequence_active = active;
}
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeEnhancedOptions(JNIEnv*, jclass) {
    static const bool wanted = [] { mods::play_context_want(true); return true; }();
    (void)wanted;
    const auto c = mods::play_context();
    const bool play = c.has(mods::playctx::kInPlay) && !mods::playctx::swimming(c) && !mods::playctx::in_ship(c);
    return (play ? enhanced::combat.get() : 0) | (enhanced::haptics.get() << 4) |
           (enhanced::double_tap.get() << 5);
}
#endif

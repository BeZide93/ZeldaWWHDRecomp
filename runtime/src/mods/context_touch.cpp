// SPDX-License-Identifier: MPL-2.0
// The context buttons' runtime for the Android touch controls (context_touch.h): the JNI the Java side
// polls. First cut (the contract): the rules' view from the last PlayContext; the touch-only state
// (items by tap, ZL latch, golden rings, assets) follows.
#include "context_touch.h"

#include "play_context.h"

namespace mods {
namespace ctxtouch_rt {
ctxtouch::Extra extra() { return {}; }
}  // namespace ctxtouch_rt
}  // namespace mods

#ifdef __ANDROID__
#include <jni.h>
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeContextButtons(JNIEnv* env, jclass, jintArray out) {
    static const bool wanted = (mods::play_context_want(true), true);
    (void)wanted;
    using namespace mods;
    int32_t v[ctxtouch::kPackLen];
    ctxtouch::pack(ctxbtn::context_buttons(play_context()), ctxtouch_rt::extra(), v, ctxtouch::kPackLen);
    if (out) {
        const jsize n = env->GetArrayLength(out);
        env->SetIntArrayRegion(out, 0, n < ctxtouch::kPackLen ? n : ctxtouch::kPackLen, reinterpret_cast<const jint*>(v));
    }
    return ctxtouch::kPackVersion;
}
extern "C" JNIEXPORT jint JNICALL Java_org_wwhdrecomp_wwhd_TouchControls_nativeContextAssets(JNIEnv*, jclass, jstring) {
    return 0;  // not ready (stub)
}
#endif

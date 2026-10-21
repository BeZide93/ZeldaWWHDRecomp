// Settings > Graphics > Compile shaders in the background (background_shaders.h): which draws may be
// left out while their shaders compile (classify, Tracker), the setting (off by default, switched at
// once, WWHD_BG_SHADERS wins), the wait budget and the performance report line.
// Run as: background_shaders_test (setting), background_shaders_test env-on, ... env-off, ... budget.
#include "background_shaders.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// portable environment edits (no setenv/unsetenv on Windows)
static void set_env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key, value, 1); else unsetenv(key);
#endif
}

static std::vector<std::string> logs;
void log_msg(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    logs.push_back(buf);
}
static bool logged(const char* text) {
    for (const auto& l : logs)
        if (l.find(text) != std::string::npos) return true;
    return false;
}
static bool has(const std::string& s, const char* text) { return s.find(text) != std::string::npos; }

using namespace bgshaders;

// the presented picture, redrawn every frame, never copied or read back
static TargetFacts picture() {
    TargetFacts t;
    t.presented = t.redrawn = true;
    return t;
}

static void test_classify() {
    TargetFacts color = picture(), depth = picture();
    depth.presented = false;  // a depth buffer is never itself presented
    // option off: every draw keeps the exact wait
    assert(classify(false, &color, 1, &depth, 36) == kOff);
    // the main picture with its depth buffer may be left out
    assert(classify(true, &color, 1, &depth, 36) == kSkippable);
    assert(classify(true, &color, 1, nullptr, 36) == kSkippable);  // depth test only, or no depth buffer
    // depth-only passes (shadow maps, depth pre-passes) keep the exact wait
    assert(classify(true, nullptr, 0, &depth, 36) == kNoColor);
    assert(classify(true, nullptr, 0, nullptr, 36) == kNoColor);
    // render to texture (reflections, screen copies, post-effect chains): not the presented picture
    TargetFacts texture = color;
    texture.presented = false;
    assert(classify(true, &texture, 1, &depth, 36) == kNotVisible);
    TargetFacts textures[2] = {texture, texture};
    assert(classify(true, textures, 2, &depth, 36) == kNotVisible);
    // an extra colour target drawn together with the picture (a per-frame mask the post effects
    // read in the same frame) is treated like the picture's depth buffer
    TargetFacts mrt[2] = {color, texture};
    assert(classify(true, mrt, 2, &depth, 36) == kSkippable);
    TargetFacts mrtFresh[2] = {color, texture};
    mrtFresh[1].redrawn = false;
    assert(classify(true, mrtFresh, 2, &depth, 36) == kFirstFrame);
    TargetFacts mrtCopied[2] = {texture, color};
    mrtCopied[0].copiedOccasionally = true;
    assert(classify(true, mrtCopied, 2, &depth, 36) == kCopied);
    TargetFacts mrtRead[2] = {color, texture};
    mrtRead[1].readBack = true;
    assert(classify(true, mrtRead, 2, nullptr, 36) == kReadBack);
    // the first frame of a target (or a target drawn now and then) may be its only result
    TargetFacts fresh = color;
    fresh.redrawn = false;
    assert(classify(true, &fresh, 1, &depth, 36) == kFirstFrame);
    TargetFacts freshDepth = depth;
    freshDepth.redrawn = false;
    assert(classify(true, &color, 1, &freshDepth, 36) == kFirstFrame);
    // read by the CPU, copied from now and then: exact
    TargetFacts read = color;
    read.readBack = true;
    assert(classify(true, &read, 1, &depth, 36) == kReadBack);
    TargetFacts readDepth = depth;
    readDepth.readBack = true;
    assert(classify(true, &color, 1, &readDepth, 36) == kReadBack);
    TargetFacts copied = color;
    copied.copiedOccasionally = true;
    assert(classify(true, &copied, 1, &depth, 36) == kCopied);
    TargetFacts copiedDepth = depth;
    copiedDepth.copiedOccasionally = true;
    assert(classify(true, &color, 1, &copiedDepth, 36) == kCopied);
    // a full-screen pass (two triangles: a fade, a filter) always waits; left out it would flash
    assert(classify(true, &color, 1, nullptr, kFullScreenVertices) == kFullScreen);
    assert(classify(true, &color, 1, nullptr, 4) == kFullScreen);
    assert(classify(true, &color, 1, nullptr, kFullScreenVertices + 1) == kSkippable);
    assert(classify(true, nullptr, 0, &depth, 3) == kNoColor);
    for (int v = 0; v < kVerdicts; v++) assert(strcmp(verdict_name(Verdict(v)), "?") != 0);
}

static void test_tracker() {
    Tracker t;
    const uint32_t scene = 0x10000000, reflection = 0x20000000, photo = 0x30000000, linear = 0x40000000;
    // nothing known: never skippable
    TargetFacts f = t.facts(scene, 10);
    assert(!f.presented && !f.redrawn && !f.readBack && !f.copiedOccasionally);
    // the picture: drawn and presented every frame
    for (uint64_t frame = 10; frame < 14; frame++) {
        t.drawn(scene, frame);
        t.presented(scene, frame);
        t.drawn(reflection, frame);  // drawn every frame, sampled by the scene, never presented
    }
    f = t.facts(scene, 14);  // the next frame, before its scan copy
    assert(f.presented && f.redrawn && !f.readBack && !f.copiedOccasionally);
    const TargetFacts sceneDepthFacts = t.facts(scene, 14);
    assert(classify(true, &f, 1, &sceneDepthFacts, 36) == kSkippable);
    TargetFacts r = t.facts(reflection, 14);
    assert(!r.presented && r.redrawn);
    assert(classify(true, &r, 1, nullptr, 36) == kNotVisible);
    // a frame without the scan copy (a loading screen, the menu shows another target): no longer the picture
    assert(!t.facts(scene, 16).presented);
    // a target not drawn in the previous frame is in its first frame again
    assert(!t.facts(scene, 16).redrawn);
    // copies in every frame (a refraction copy, an effect's input) refresh their result every frame
    for (uint64_t frame = 20; frame < 24; frame++) {
        t.drawn(scene, frame);
        t.presented(scene, frame);
        assert(!t.copied(scene, frame));
    }
    f = t.facts(scene, 24);
    assert(f.presented && f.redrawn && !f.copiedOccasionally);
    // a single copy (a photo, a menu background) keeps the target exact for kQuietFrames
    for (uint64_t frame = 30; frame < 40; frame++) {
        t.drawn(photo, frame);
        t.presented(photo, frame);
    }
    t.copied(photo, 39);
    for (uint64_t frame = 40; frame < 39 + kQuietFrames; frame++) {
        t.drawn(photo, frame);
        t.presented(photo, frame);
        assert(t.facts(photo, frame + 1).copiedOccasionally);
    }
    assert(!t.facts(photo, 40 + kQuietFrames).copiedOccasionally);
    // a second copy after a gap starts a new run: occasional again
    t.copied(photo, 300);
    assert(t.facts(photo, 301).copiedOccasionally);
    // the copy of a target in the same frame that left a draw into it out is counted
    reset_counters();
    t.left_out(photo, 302);
    assert(t.copied(photo, 302));
    assert(counters().copiesAfterSkip == 1);
    assert(!t.copied(photo, 303));
    // read by the CPU: exact for kQuietFrames
    t.drawn(linear, 50);
    t.read_back(linear, 50);
    assert(t.facts(linear, 51).readBack && t.facts(linear, 50 + kQuietFrames).readBack);
    assert(!t.facts(linear, 51 + kQuietFrames).readBack);
    // a loaded save state forgets everything
    t.forget();
    assert(t.size() == 0 && !t.facts(scene, 24).presented);
}

static void test_budget_and_counters() {
    reset_counters();
    const uint64_t budget = uint64_t(wait_budget_ms() * 1e6);
    assert(budget_left_ns(7) == budget);
    spent(7, kFastWaitNs / 2, false);  // a fast result (a warm cache) uses none of the budget
    assert(budget_left_ns(7) == budget);
    spent(7, budget / 2, false);
    assert(budget_left_ns(7) == budget - budget / 2);
    spent(7, budget, true);
    assert(budget_left_ns(7) == 0);  // the frame's budget is used up: later draws are left out at once
    assert(budget_left_ns(8) == budget);  // a new frame, a new budget
    assert(!skipped_in_frame(8));
    skipped(8);
    skipped(8);
    skipped(9);
    assert(skipped_in_frame(9) && !skipped_in_frame(8));
    peek_withheld();
    exact(kNoColor);
    exact(kNotVisible);
    exact(kNotVisible);
    const Counters c = counters();
    assert(c.skipped == 3 && c.skipFrames == 2 && c.budgetWaits == 3 && c.budgetRanOut == 1 && c.peeksWithheld == 1);
    assert(c.exact[kNoColor] == 1 && c.exact[kNotVisible] == 2);
    const std::string line = report_line();
    assert(has(line, "Draws left out while compiling: 3 in 2 frames"));
    assert(has(line, "not the picture 2") && has(line, "no colour target 1") && has(line, "depth peeks kept: 1"));
    reset_counters();
    assert(has(report_line(), "Exact waits: none"));
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "budget") {  // WWHD_BG_SHADER_WAIT_MS sets the frame's budget
        set_env("WWHD_BG_SHADER_WAIT_MS", "2.5");
        assert(wait_budget_ms() == 2.5);
        reset_counters();
        assert(budget_left_ns(1) == 2500000);
        return 0;
    }
    if (mode == "env-on" || mode == "env-off") {
        const bool on = mode == "env-on";
        set_env("WWHD_BG_SHADERS", on ? "1" : "0");
        start(!on);  // the saved setting says the opposite: the variable wins
        assert(enabled() == on && env_override());
        assert(logged(on ? "compile in the background on (WWHD_BG_SHADERS=1)" : "compile in the background off (WWHD_BG_SHADERS=0)"));
        set_requested(on);
        assert(enabled() == on);
        assert(logged("decides for this start"));
        assert(has(report_line(), on ? "on (WWHD_BG_SHADERS=1)" : "off (WWHD_BG_SHADERS=0)"));
        return 0;
    }
    set_env("WWHD_BG_SHADERS", nullptr);
    set_env("WWHD_BG_SHADER_WAIT_MS", nullptr);
    set_env("WWHD_COMPILE_WAIT_MS", nullptr);
    assert(!strcmp(kSettingKey, "backgroundShaders"));
    // default: never saved means off
    assert(!enabled() && !env_override());
    start(false);
    assert(!enabled() && logged("compile in the background off (setting)"));
    assert(wait_budget_ms() == kDefaultWaitMs);
    // switched at once, both ways, each change logged once
    logs.clear();
    set_requested(true);
    assert(enabled() && logged("compile in the background on (setting)"));
    set_requested(true);
    assert(logs.size() == 1);
    set_requested(false);
    assert(!enabled() && logged("compile in the background off (setting)"));
    // a saved "on" is in effect from the start
    start(true);
    assert(enabled());
    start(false);
    test_classify();
    test_tracker();
    test_budget_and_counters();
    assert(has(report_line(), "Background shader compile: off (setting)"));
    puts("background_shaders_test: ok");
    return 0;
}

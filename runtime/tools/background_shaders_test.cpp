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
    assert(classify(kLevelOff, &color, 1, &depth, 36) == kOff);
    // the main picture with its depth buffer may be left out
    assert(classify(kLevelSafe, &color, 1, &depth, 36) == kSkippable);
    assert(classify(kLevelSafe, &color, 1, nullptr, 36) == kSkippable);  // depth test only, or no depth buffer
    // depth-only passes (shadow maps, depth pre-passes) keep the exact wait
    assert(classify(kLevelSafe, nullptr, 0, &depth, 36) == kNoColor);
    assert(classify(kLevelSafe, nullptr, 0, nullptr, 36) == kNoColor);
    // render to texture (reflections, screen copies, post-effect chains): not the presented picture
    TargetFacts texture = color;
    texture.presented = false;
    assert(classify(kLevelSafe, &texture, 1, &depth, 36) == kNotVisible);
    TargetFacts textures[2] = {texture, texture};
    assert(classify(kLevelSafe, textures, 2, &depth, 36) == kNotVisible);
    // an extra colour target drawn together with the picture (a per-frame mask the post effects
    // read in the same frame) is treated like the picture's depth buffer
    TargetFacts mrt[2] = {color, texture};
    assert(classify(kLevelSafe, mrt, 2, &depth, 36) == kSkippable);
    TargetFacts mrtFresh[2] = {color, texture};
    mrtFresh[1].redrawn = false;
    assert(classify(kLevelSafe, mrtFresh, 2, &depth, 36) == kFirstFrame);
    TargetFacts mrtCopied[2] = {texture, color};
    mrtCopied[0].copiedOccasionally = true;
    assert(classify(kLevelSafe, mrtCopied, 2, &depth, 36) == kCopied);
    TargetFacts mrtRead[2] = {color, texture};
    mrtRead[1].readBack = true;
    assert(classify(kLevelSafe, mrtRead, 2, nullptr, 36) == kReadBack);
    // the first frame of a target (or a target drawn now and then) may be its only result
    TargetFacts fresh = color;
    fresh.redrawn = false;
    assert(classify(kLevelSafe, &fresh, 1, &depth, 36) == kFirstFrame);
    TargetFacts freshDepth = depth;
    freshDepth.redrawn = false;
    assert(classify(kLevelSafe, &color, 1, &freshDepth, 36) == kFirstFrame);
    // read by the CPU, copied from now and then: exact
    TargetFacts read = color;
    read.readBack = true;
    assert(classify(kLevelSafe, &read, 1, &depth, 36) == kReadBack);
    TargetFacts readDepth = depth;
    readDepth.readBack = true;
    assert(classify(kLevelSafe, &color, 1, &readDepth, 36) == kReadBack);
    TargetFacts copied = color;
    copied.copiedOccasionally = true;
    assert(classify(kLevelSafe, &copied, 1, &depth, 36) == kCopied);
    TargetFacts copiedDepth = depth;
    copiedDepth.copiedOccasionally = true;
    assert(classify(kLevelSafe, &color, 1, &copiedDepth, 36) == kCopied);
    // a full-screen pass (two triangles: a fade, a filter) always waits; left out it would flash
    assert(classify(kLevelSafe, &color, 1, nullptr, kFullScreenVertices) == kFullScreen);
    assert(classify(kLevelSafe, &color, 1, nullptr, 4) == kFullScreen);
    assert(classify(kLevelSafe, &color, 1, nullptr, kFullScreenVertices + 1) == kSkippable);
    assert(classify(kLevelSafe, nullptr, 0, &depth, 3) == kNoColor);
    // the depth buffer the game peeks (the sun's glare test) keeps Safe's draws into it exact
    TargetFacts peekedDepth = depth;
    peekedDepth.peeked = true;
    assert(classify(kLevelSafe, &color, 1, &peekedDepth, 36) == kReadBack);
    for (int v = 0; v < kVerdicts; v++) assert(strcmp(verdict_name(Verdict(v)), "?") != 0);
}

// Fast: anything may be left out except what the game reads back or keeps
static void test_classify_fast() {
    TargetFacts color, depth, texture, shadow;
    color.presented = color.redrawn = color.regular = true;
    depth.redrawn = depth.regular = true;
    texture.redrawn = texture.regular = true;  // a reflection or post-effect target, redrawn every frame
    shadow = texture;                         // a shadow map's depth buffer
    assert(classify(kLevelFast, &color, 1, &depth, 36) == kSkippable);
    assert(classify(kLevelFast, &texture, 1, nullptr, 36) == kSkippable);   // render to texture
    assert(classify(kLevelFast, nullptr, 0, &shadow, 36) == kSkippable);    // shadow map
    assert(classify(kLevelFast, &color, 1, nullptr, 4) == kSkippable);      // full-screen pass
    TargetFacts peeked = depth;
    peeked.peeked = true;  // the sun's glare test reads it: an older answer would change the game
    assert(classify(kLevelFast, &color, 1, &peeked, 36) == kReadBack);
    TargetFacts colorOnly = color;  // a draw that only tests that depth buffer can still be left out
    assert(classify(kLevelFast, &colorOnly, 1, nullptr, 36) == kSkippable);
    // the first frame after a load: the target was redrawn regularly before
    TargetFacts afterLoad = color;
    afterLoad.redrawn = false;
    assert(classify(kLevelFast, &afterLoad, 1, &depth, 36) == kSkippable);
    // a new target, or one drawn only now and then (a one-off capture: a photo, a snapshot)
    TargetFacts oneOff = texture;
    oneOff.redrawn = oneOff.regular = false;
    assert(classify(kLevelFast, &oneOff, 1, nullptr, 36) == kFirstFrame);
    assert(classify(kLevelFast, nullptr, 0, &oneOff, 36) == kFirstFrame);
    TargetFacts mixed[2] = {color, oneOff};
    assert(classify(kLevelFast, mixed, 2, &depth, 36) == kFirstFrame);
    // read by the CPU, copied from now and then: exact
    TargetFacts read = texture;
    read.readBack = true;
    assert(classify(kLevelFast, &read, 1, nullptr, 36) == kReadBack);
    TargetFacts copied = color;
    copied.copiedOccasionally = true;
    assert(classify(kLevelFast, &copied, 1, &depth, 36) == kCopied);
    TargetFacts copiedDepth = depth;
    copiedDepth.copiedOccasionally = true;
    assert(classify(kLevelFast, nullptr, 0, &copiedDepth, 36) == kCopied);
    assert(classify(kLevelOff, &color, 1, &depth, 36) == kOff);
}

static void test_regular_and_forget() {
    Tracker t;
    const uint32_t scene = 0x10000000, photo = 0x30000000;
    t.drawn(scene, 1);
    t.drawn(scene, 2);
    assert(!t.facts(scene, 3).regular);  // two frames are not yet regular
    t.drawn(scene, 3);
    t.drawn(scene, 3);  // drawn again in the same frame: still one frame
    assert(t.facts(scene, 4).regular && t.facts(scene, 4).redrawn);
    // a target drawn every other frame never becomes regular
    for (uint64_t frame = 10; frame < 30; frame += 2) t.drawn(photo, frame);
    assert(!t.facts(photo, 31).regular);
    // a load: frame facts go, the regular targets are remembered
    t.presented(scene, 3);
    t.peeked(scene, 3);
    t.forget();
    TargetFacts f = t.facts(scene, 100);
    assert(f.regular && !f.redrawn && !f.presented && !f.peeked);
    assert(!t.facts(photo, 100).regular && t.size() == 1);
    // the first frame after the load: Fast leaves it out, Safe waits
    TargetFacts picture = f;
    picture.presented = true;
    assert(classify(kLevelFast, &picture, 1, nullptr, 36) == kSkippable);
    assert(classify(kLevelSafe, &picture, 1, nullptr, 36) == kFirstFrame);
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
    assert(classify(kLevelSafe, &f, 1, &sceneDepthFacts, 36) == kSkippable);
    TargetFacts r = t.facts(reflection, 14);
    assert(!r.presented && r.redrawn);
    assert(classify(kLevelSafe, &r, 1, nullptr, 36) == kNotVisible);
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
    assert(t.left_out_in(photo, 302) && !t.left_out_in(photo, 303) && !t.left_out_in(linear, 302));
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
    assert(!t.facts(scene, 24).presented && !t.facts(linear, 51).readBack);
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
    if (mode == "env-safe" || mode == "env-fast" || mode == "env-off") {
        const char* value = mode.c_str() + 4;  // safe, fast, off
        const Level want = mode == "env-safe" ? kLevelSafe : mode == "env-fast" ? kLevelFast : kLevelOff;
        set_env("WWHD_BG_SHADERS", want == kLevelOff ? "0" : value);
        start(want == kLevelFast ? kLevelOff : kLevelFast);  // the saved setting differs: the variable wins
        assert(level() == want && env_override());
        const std::string started = std::string("shader compile ") + level_name(want) + " (WWHD_BG_SHADERS=";
        assert(logged(started.c_str()));
        set_level(kLevelSafe == want ? kLevelOff : kLevelSafe);
        assert(level() == want);
        assert(logged("decides for this start"));
        assert(has(report_line(), (std::string("Shader compile: ") + level_name(want) + " (WWHD_BG_SHADERS=").c_str()));
        return 0;
    }
    set_env("WWHD_BG_SHADERS", nullptr);
    set_env("WWHD_BG_SHADER_WAIT_MS", nullptr);
    assert(!strcmp(kSettingKey, "backgroundShaders"));
    // saved values: "0", "safe", "fast"; never saved (or unknown) means safe, the default
    assert(parse_level("safe") == kLevelSafe && parse_level("fast") == kLevelFast && parse_level("0") == kLevelOff);
    assert(parse_level("off") == kLevelOff && parse_level(nullptr) == kLevelSafe && parse_level("") == kLevelSafe);
    for (int l = 0; l < kLevels; l++) assert(parse_level(level_value(Level(l))) == Level(l));
    // default: never saved means Safe
    assert(!env_override() && level() == kLevelSafe && enabled());
    start(parse_level(nullptr));
    assert(level() == kLevelSafe && logged("shader compile safe (setting)"));
    assert(wait_budget_ms() == kDefaultWaitMs);
    // switched at once, each change logged once
    logs.clear();
    set_level(kLevelFast);
    assert(level() == kLevelFast && enabled() && logged("shader compile fast (setting)"));
    set_level(kLevelFast);
    assert(logs.size() == 1);
    set_level(kLevelOff);
    assert(!enabled() && logged("shader compile off (setting)"));
    set_level(kLevelSafe);
    assert(level() == kLevelSafe && logged("shader compile safe (setting)"));
    // a saved level is in effect from the start
    start(kLevelFast);
    assert(level() == kLevelFast && requested() == kLevelFast);
    start(kLevelOff);
    test_classify();
    test_classify_fast();
    test_tracker();
    test_regular_and_forget();
    test_budget_and_counters();
    assert(has(report_line(), "Shader compile: off (setting)"));
    puts("background_shaders_test: ok");
    return 0;
}

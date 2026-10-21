// Settings > Graphics > "Compile shaders in the background" (both renderers).
//
// Off (the default): a draw whose shader or pipeline is still compiling waits for it, so every frame
// is exact (Vulkan repeats the last finished picture meanwhile).
// On: such a draw waits at most a short budget per frame (WWHD_BG_SHADER_WAIT_MS, default
// kDefaultWaitMs, shared by all draws of the frame) and is then left out of this frame while the
// compile goes on in the background; it is drawn normally once ready. Only draws whose result is
// nothing but the visible picture may be left out (classify() below); all others keep the exact wait.
//
// Saved as backgroundShaders ("1"/"0") with the other settings on every host. WWHD_BG_SHADERS=1 / =0
// forces it on / off for that start (the setting is then not used). A change applies at once: the
// choice is made per draw. The log records the state at start-up and each change.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "gfx/target_draw_history.h"

namespace bgshaders {

constexpr const char* kSettingKey = "backgroundShaders";  // hostui setting: "1" on, "0" off
constexpr double kDefaultWaitMs = 4.0;                     // the frame's wait budget while on

const char* env_override();   // WWHD_BG_SHADERS when set (it wins over the setting), else nullptr
bool enabled();               // in effect now: WWHD_BG_SHADERS, else the setting (any thread)
void set_requested(bool on);  // the setting changed (any thread); applies to the next draw
void start(bool saved);       // start-up: the saved setting; logs the state
double wait_budget_ms();      // WWHD_BG_SHADER_WAIT_MS, else kDefaultWaitMs

// ---- which draws may be left out
// Why a draw keeps the exact wait (or kSkippable: it may be left out after the budget).
enum Verdict : uint8_t {
    kSkippable,
    kOff,           // the option is off
    kNoColor,       // writes no colour target: depth-only passes (shadow maps, depth pre-passes)
    kNotVisible,    // writes no colour target that is the presented picture (render to texture,
                    // reflections, screen copies, post-effect chains, the GamePad's own targets ...)
    kFirstFrame,    // a written target was not drawn in the previous frame: the result may be its only one
    kReadBack,      // a written target was read by the CPU in the last kQuietFrames
    kCopied,        // a written target was copied from in the last kQuietFrames, but not every frame
    kFullScreen,    // at most kFullScreenVertices vertices: a full-screen pass (a fade, a filter); left
                    // out it would flash the whole picture
    kVerdicts
};
const char* verdict_name(Verdict v);

constexpr uint64_t kQuietFrames = 120;  // how long a readback or an occasional copy keeps a target exact
constexpr uint32_t kFullScreenVertices = 6;  // draws this small (two triangles) always wait
// A wait shorter than this does not use the frame's budget: a pipeline found in the driver's cache
// (a warm cache) is ready in well under a millisecond, so warm frames behave as with the option off.
constexpr uint64_t kFastWaitNs = 1000000;

// What the tracker knows about one written target (colour, or depth/stencil).
struct TargetFacts {
    bool presented = false;   // the source of a TV or GamePad scan copy in this or the previous frame
    bool redrawn = false;     // drawn in the previous frame too (not its first frame: TargetDrawHistory)
    bool readBack = false;    // read by the CPU in the last kQuietFrames
    bool copiedOccasionally = false;  // copied from in the last kQuietFrames, without a copy every frame
};
// Pure rule (unit tested). colors: the written colour targets (write mask not zero); depth: the depth
// target when the draw writes depth or stencil, else nullptr; the depth buffer whose pixels the game
// reads back (its depth peeks for the sun's glare) counts as read back. A draw may be left out when one of its
// colour targets is the presented picture and every target it writes (the picture, extra colour
// targets drawn with it in the same draw, its depth buffer) is redrawn every frame, not read by the
// CPU and not copied from now and then: then all it writes is a by-product of this frame's picture.
// vertices: the draw's vertex (index) count.
Verdict classify(bool on, const TargetFacts* colors, int colorCount, const TargetFacts* depth, uint32_t vertices);

// Per guest surface address: what the game did with it, by frame. Render thread only.
class Tracker {
public:
    void drawn(uint32_t addr, uint64_t frame) { use_[addr].history.drawn(frame); }
    void presented(uint32_t addr, uint64_t frame) { use_[addr].presented = frame; }
    // a copy from the target; true when a draw into it was left out in this frame (counted)
    bool copied(uint32_t addr, uint64_t frame);
    void left_out(uint32_t addr, uint64_t frame) { use_[addr].leftOut = frame; }
    void read_back(uint32_t addr, uint64_t frame) { use_[addr].readBack = frame; }
    TargetFacts facts(uint32_t addr, uint64_t frame) const;
    void forget() { use_.clear(); }  // save state loaded: surfaces were forgotten
    size_t size() const { return use_.size(); }
private:
    static constexpr uint64_t kNever = ~uint64_t{0};
    struct Use {
        gfx::TargetDrawHistory history;
        uint64_t presented = kNever, readBack = kNever, leftOut = kNever;
        uint64_t copied = kNever, copyRunStart = kNever;  // last copy, first frame of the current run of
                                                          // copies in consecutive frames
    };
    std::unordered_map<uint32_t, Use> use_;
};
Tracker& tracker();

// ---- the frame's wait budget and the counters
// Budget left in this frame, in nanoseconds (the renderer's frame number starts a new budget).
uint64_t budget_left_ns(uint64_t frame);
void spent(uint64_t frame, uint64_t ns, bool ranOut);
void skipped(uint64_t frame);          // a draw was left out
void exact(Verdict v);                 // a draw with a compile pending kept the exact wait (v: why)
bool skipped_in_frame(uint64_t frame); // a draw was left out in this frame (depth peeks keep the old answer)
void peek_withheld();                  // a depth peek was not answered because of that
struct Counters {
    uint64_t skipped = 0, skipFrames = 0, budgetWaits = 0, budgetRanOut = 0, budgetWaitNs = 0, peeksWithheld = 0;
    uint64_t copiesAfterSkip = 0;  // copies from a target in a frame that left a draw into it out
    uint64_t exact[kVerdicts] = {};
};
Counters counters();
std::string report_line();  // one line for the performance report and the log
void reset_counters();      // tests

}  // namespace bgshaders

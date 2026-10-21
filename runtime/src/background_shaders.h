// Settings > Graphics > "Shader compile" (both renderers): what a draw does when its shader or
// pipeline is still compiling.
//
// Off (exact): every draw waits for its compile, so every frame is complete (Vulkan repeats the last
//   finished picture meanwhile).
// Safe (the default): a draw waits at most a short budget per frame (WWHD_BG_SHADER_WAIT_MS, default kDefaultWaitMs,
//   shared by all draws of the frame) and is then left out of this frame while the compile goes on in
//   the background; it is drawn normally once ready. Only draws whose result is nothing but the visible
//   picture may be left out (classify() below); all others keep the exact wait.
// Fast (not exact, like Cemu's async shader compile): after the same budget any draw may be left out,
//   shadow maps, render to texture, post effects and the first frame after a load included. Only what
//   could change the game's behaviour or leave a lasting wrong picture keeps the exact wait: targets the
//   CPU reads back or the game depth-peeks, targets copied from now and then (snapshots, photos), and targets that are new or
//   drawn only now and then (a one-off capture). Presentation and overlay never take this path.
// In both levels the depth buffer the game peeks (the sun's glare test, read every frame while the sun
// is up) keeps the exact wait: an answer from an older frame changes the game's glare state and with it
// its random numbers (measured: other clouds on the title screen). Only when a draw into that buffer was
// left out before the first peek does that frame answer no peek; the game keeps its previous answer.
//
// Saved as backgroundShaders ("0", "safe", "fast"; never saved means safe) with the other settings
// on every host. WWHD_BG_SHADERS=0|safe|fast forces the level for that start (the setting is then not
// used). A change applies at once: the choice is made per draw. The log records the level at start-up
// and each change.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include "gfx/target_draw_history.h"

namespace bgshaders {

constexpr const char* kSettingKey = "backgroundShaders";  // hostui setting: "0", "safe", "fast"
constexpr double kDefaultWaitMs = 4.0;                     // the frame's wait budget (Safe, Fast)
constexpr uint8_t kDefaultLevel = 1;                       // kLevelSafe

enum Level : uint8_t { kLevelOff, kLevelSafe, kLevelFast, kLevels };
const char* level_name(Level l);        // "off", "safe", "fast"
const char* level_value(Level l);       // what the setting saves: "0", "safe", "fast"
Level parse_level(const char* value);   // "0"/"off" -> off, "fast" -> fast, anything else (missing) safe

const char* env_override();   // WWHD_BG_SHADERS when set (it wins over the setting), else nullptr
Level level();                // in effect now: WWHD_BG_SHADERS, else the setting (any thread)
inline bool enabled() { return level() != kLevelOff; }
Level requested();            // the setting (what the Graphics tab shows when no override is set)
void set_level(Level l);      // the setting changed (any thread); applies to the next draw
void start(Level saved);      // start-up: the saved setting; logs the level
double wait_budget_ms();      // WWHD_BG_SHADER_WAIT_MS, else kDefaultWaitMs

// ---- which draws may be left out
// Why a draw keeps the exact wait (or kSkippable: it may be left out after the budget).
enum Verdict : uint8_t {
    kSkippable,
    kOff,           // the setting is Off (exact)
    kNoColor,       // Safe: writes no colour target: depth-only passes (shadow maps, depth pre-passes)
    kNotVisible,    // Safe: writes no colour target that is the presented picture (render to texture,
                    // reflections, screen copies, post-effect chains, the GamePad's own targets ...)
    kFirstFrame,    // Safe: a written target was not drawn in the previous frame (its result may be
                    // its only one). Fast: the same, for a target never drawn in kRegularFrames
                    // consecutive frames (new, or drawn only now and then: a one-off capture)
    kReadBack,      // a written target was read by the CPU or depth-peeked in the last kQuietFrames
    kCopied,        // a written target was copied from in the last kQuietFrames, but not every frame
    kFullScreen,    // Safe: at most kFullScreenVertices vertices: a full-screen pass (a fade, a
                    // filter); left out it would flash the whole picture
    kVerdicts
};
const char* verdict_name(Verdict v);

constexpr uint64_t kQuietFrames = 120;  // how long a readback or an occasional copy keeps a target exact
constexpr uint32_t kFullScreenVertices = 6;  // Safe: draws this small (two triangles) always wait
constexpr uint64_t kRegularFrames = 3;  // Fast: drawn in this many consecutive frames = redrawn regularly
// A wait shorter than this does not use the frame's budget: a pipeline found in the driver's cache
// (a warm cache) is ready in well under a millisecond, so warm frames behave as with the option off.
constexpr uint64_t kFastWaitNs = 1000000;

// What the tracker knows about one written target (colour, or depth/stencil).
struct TargetFacts {
    bool presented = false;   // the source of a TV or GamePad scan copy in this or the previous frame
    bool redrawn = false;     // drawn in the previous frame too (not its first frame: TargetDrawHistory)
    bool regular = false;     // was drawn in kRegularFrames consecutive frames at some time
    bool readBack = false;    // read by the CPU in the last kQuietFrames
    bool peeked = false;      // depth-peeked by the game in the last kQuietFrames
    bool copiedOccasionally = false;  // copied from in the last kQuietFrames, without a copy every frame
};
// Pure rule (unit tested). colors: the written colour targets (write mask not zero); depth: the depth
// target when the draw writes depth or stencil, else nullptr; vertices: the draw's vertex (index) count.
// Safe: a draw may be left out when one of its colour targets is the presented picture and every
// target it writes (the picture, extra colour targets drawn with it in the same draw, its depth buffer)
// is redrawn every frame, not read back or depth-peeked and not copied from now and then: then all it
// writes is a by-product of this frame's picture. Fast: any draw, unless a target it writes is read
// back by the CPU or depth-peeked, copied from now and then, or new / drawn only now and then.
Verdict classify(Level level, const TargetFacts* colors, int colorCount, const TargetFacts* depth,
                 uint32_t vertices);

// Per guest surface address: what the game did with it, by frame. Render thread only.
class Tracker {
public:
    void drawn(uint32_t addr, uint64_t frame);
    void presented(uint32_t addr, uint64_t frame) { use_[addr].presented = frame; }
    // a copy from the target; true when a draw into it was left out in this frame (counted)
    bool copied(uint32_t addr, uint64_t frame);
    void left_out(uint32_t addr, uint64_t frame) { use_[addr].leftOut = frame; }
    bool left_out_in(uint32_t addr, uint64_t frame) const {  // a draw into it was left out in this frame
        auto it = use_.find(addr);
        return it != use_.end() && it->second.leftOut == frame;
    }
    void read_back(uint32_t addr, uint64_t frame) { use_[addr].readBack = frame; }
    void peeked(uint32_t addr, uint64_t frame) { use_[addr].peeked = frame; }
    TargetFacts facts(uint32_t addr, uint64_t frame) const;
    // save state loaded: surfaces were forgotten. What each frame did is dropped; which targets the
    // game redraws regularly (the picture, its buffers) is kept: the addresses are the game's own.
    void forget();
    size_t size() const { return use_.size(); }
private:
    static constexpr uint64_t kNever = ~uint64_t{0};
    struct Use {
        gfx::TargetDrawHistory history;
        uint64_t presented = kNever, readBack = kNever, peeked = kNever, leftOut = kNever;
        uint64_t copied = kNever, copyRunStart = kNever;  // last copy, first frame of the current run of
                                                          // copies in consecutive frames
        uint64_t drawRunStart = kNever;  // first frame of the current run of consecutive drawn frames
        bool regular = false;
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

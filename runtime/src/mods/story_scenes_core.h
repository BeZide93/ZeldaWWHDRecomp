// Host-side scene lifecycle. No guest pointers or game save bits are stored here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>

namespace story_scenes {
enum class StopReason { Finished, Skipped, Aborted, StateLoaded, Interrupted, Failed, TimedOut };
enum class StepResult { Running, Complete, Failed };
struct Timing { unsigned frames = 0; bool terminal = false; };

// The owner must verify every effect before acquiring event ownership. The engine never treats
// an unimplemented effect as a successful step. release() must restore camera/control/message
// state for every stop reason, including a guest-memory generation change.
class Driver {
public:
    virtual ~Driver() = default;
    virtual bool acquire() = 0;
    virtual bool enter(size_t step) = 0;
    virtual StepResult update(size_t step, unsigned elapsed) = 0;
    virtual bool finish() = 0; // apply mod-owned end actions; never on abort/skip
    virtual void release(StopReason reason) = 0;
};

class Player {
public:
    Player() = default;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;
    bool start(std::vector<Timing> steps, Driver& driver, uint64_t generation, bool game_event_running) {
        if (active_ || game_event_running || steps.empty() || steps.size() > 64 || !steps.back().terminal) return false;
        uint64_t total = 0;
        for (size_t i = 0; i < steps.size(); ++i) {
            if (steps[i].terminal != (i + 1 == steps.size()) || (!steps[i].terminal && !steps[i].frames)) return false;
            total += steps[i].frames;
        }
        if (total > 18000 || !driver.acquire()) return false;
        steps_ = std::move(steps);
        driver_ = &driver;
        generation_ = generation;
        index_ = elapsed_ = total_ = 0;
        active_ = true;
        if (!driver_->enter(0)) stop(StopReason::Failed);
        return active_;
    }
    void tick(uint64_t generation, bool interrupted = false, bool skip = false) {
        if (!active_) return;
        if (generation != generation_) return stop(StopReason::StateLoaded);
        if (interrupted) return stop(StopReason::Interrupted);
        if (skip) return stop(StopReason::Skipped);
        const Timing& step = steps_[index_];
        if (step.terminal) {
            const bool ok = driver_->finish();
            return stop(ok ? StopReason::Finished : StopReason::Failed);
        }
        if (++total_ > 18000) return stop(StopReason::TimedOut);
        const StepResult result = driver_->update(index_, ++elapsed_);
        if (result == StepResult::Failed) return stop(StopReason::Failed);
        // Durations are deadlines: dialogue/movement that fails to complete aborts instead of
        // advancing and silently applying its rewards. A wait backend completes at its duration.
        if (result == StepResult::Running) {
            if (elapsed_ >= step.frames) stop(StopReason::TimedOut);
            return;
        }
        ++index_;
        elapsed_ = 0;
        if (!driver_->enter(index_)) stop(StopReason::Failed);
    }
    void stop(StopReason reason = StopReason::Aborted) {
        if (!active_) return;
        active_ = false;
        Driver* driver = driver_;
        driver_ = nullptr;
        last_ = reason;
        driver->release(reason);
        steps_.clear();
    }
    bool active() const { return active_; }
    size_t step() const { return index_; }
    unsigned elapsed() const { return elapsed_; }
    StopReason last_reason() const { return last_; }
private:
    std::vector<Timing> steps_;
    Driver* driver_ = nullptr;
    uint64_t generation_ = 0;
    size_t index_ = 0;
    unsigned elapsed_ = 0, total_ = 0;
    bool active_ = false;
    StopReason last_ = StopReason::Aborted;
};
} // namespace story_scenes

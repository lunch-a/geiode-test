#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

// Independent, deterministic retry planner. No Geode types here, so it can
// be unit-tested without Geometry Dash.
namespace qfs {
struct RetryPlanner {
    using Tick = std::int64_t;

    Tick tick = 0;                  // number of processed simulation steps
    Tick checkpoint = 0;            // last stored practice checkpoint
    Tick nextCheckpoint = 60;       // due at or after this tick
    Tick clickAt = 1;               // target press simulation step
    Tick interval = 60;
    int retries = 0;
    int maxRetries = 500;
    bool exhausted = false;

    void begin(Tick initial, Tick framesPerQuarter, int limit) {
        tick = initial;
        interval = std::max<Tick>(1, framesPerQuarter);
        checkpoint = tick;
        nextCheckpoint = tick + interval;
        clickAt = tick + 1;
        retries = 0;
        maxRetries = std::max(1, limit);
        exhausted = false;
    }

    // Called only after a checkpoint was successfully created in GD.
    void savedCheckpoint() {
        checkpoint = tick;
        nextCheckpoint = tick + interval;
        clickAt = tick + 1;
        retries = 0;
    }

    // A death on step D makes D-1 the first new candidate. Subsequent
    // deaths strictly move earlier, even when the death frame changes.
    // Return false when the current section's search space is exhausted.
    bool onDeath(Tick deathStep) {
        if (exhausted) return false;
        ++retries;
        Tick beforeDeath = deathStep - 1;
        Tick candidate = retries == 1
            ? beforeDeath
            : std::min(clickAt - 1, beforeDeath);
        if (retries > maxRetries || candidate <= checkpoint) {
            exhausted = true;
            return false;
        }
        clickAt = candidate;
        return true;
    }

    void rewindToCheckpoint() { tick = checkpoint; }
    void advanceStep() { ++tick; }
    bool needsCheckpoint() const { return tick >= nextCheckpoint; }
    bool shouldPress() const { return !exhausted && tick == clickAt; }
};
}

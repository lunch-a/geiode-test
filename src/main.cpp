#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include "RetryPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace {
struct SavedPoint {
    qfs::RetryPlanner::Tick tick = 0;
    CheckpointObject* handle = nullptr; // non-owning identity, never dereferenced
};

struct Runtime {
    PlayLayer* owner = nullptr;
    cocos2d::CCLabelBMFont* hud = nullptr; // Owned by the PlayLayer scene
    qfs::RetryPlanner planner;
    bool active = false;
    bool deathPending = false;
    bool backtrackPending = false;
    std::vector<SavedPoint> savedPoints;
    int backtracks = 0;
    bool pressed = false;
    bool won = false;
    bool stopped = false;
    int lastHudTick = -1;
    int pressesThisAttempt = 0;
    int totalPresses = 0;
    int deaths = 0;
    qfs::RetryPlanner::Tick lastDeathTick = -1;
    std::string stopReason;

};
Runtime g;

bool settingEnabled() {
    return Mod::get()->getSettingValue<bool>("enabled");
}
int ticksPerSecond() {
    return std::clamp<int>(
        static_cast<int>(Mod::get()->getSettingValue<int64_t>("hertz")), 60, 1000
    );
}
int stepsPerRender() {
    return std::clamp<int>(
        static_cast<int>(Mod::get()->getSettingValue<int64_t>("steps-per-render")), 1, 32
    );
}
int retryLimit() {
    return std::clamp<int>(
        static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-retries")), 1, 100000
    );
}

bool usable(PlayLayer* pl) {
    return pl && g.owner == pl && g.active && !g.stopped &&
           settingEnabled() && pl->m_isPracticeMode &&
           !pl->m_isPaused && !pl->m_hasCompletedLevel;
}

void qfsReleaseJump(PlayLayer* pl) {
    if (g.pressed && pl) {
        pl->handleButton(false, 1, true); // Player 1 jump released
        g.pressed = false;
    }
}

void updateHUD(PlayLayer* pl, bool force = false) {
    if (!pl || !g.hud) return;
    if (!Mod::get()->getSettingValue<bool>("show-hud")) {
        g.hud->setVisible(false);
        return;
    }
    g.hud->setVisible(true);
    if (!force && static_cast<int>(g.planner.tick % 10) != 0) return;
    std::string message;
    if (g.won) message = "QFS: complete";
    else if (g.stopped) message = "QFS STOP: " + g.stopReason;
    else message = "QFS: running";
    message += "\nCP=" + std::to_string(g.planner.checkpoint) +
        " tick=" + std::to_string(g.planner.tick) +
        " target=" + std::to_string(g.planner.clickAt) +
        " retry=" + std::to_string(g.planner.retries) +
        " clicks=" + std::to_string(g.totalPresses) +
        " deaths=" + std::to_string(g.deaths) +
        " cps=" + std::to_string(g.savedPoints.size()) +
        " backs=" + std::to_string(g.backtracks);
    if (g.lastDeathTick >= 0)
        message += " lastDeath=" + std::to_string(g.lastDeathTick);
    g.hud->setString(message.c_str());
}

void start(PlayLayer* pl) {
    g.active = true;
    g.deathPending = false;
    g.backtrackPending = false;
    g.savedPoints.clear();
    g.backtracks = 0;
    g.stopped = false;
    g.won = false;
    g.pressed = false;
    g.pressesThisAttempt = 0;
    g.totalPresses = 0;
    g.deaths = 0;
    g.lastDeathTick = -1;
    g.stopReason.clear();
    g.planner.begin(0, std::max(1, (ticksPerSecond() + 2) / 4), retryLimit());

    // Start with a real GD checkpoint, not a fake saved x/y position. This
    // also saves triggers, game state, and other information that GD tracks.
    auto* rootCheckpoint = pl->markCheckpoint();
    if (rootCheckpoint == nullptr) {
        g.active = false;
        g.stopped = true;
        g.stopReason = "initial checkpoint failed";
        log::warn("QFS: markCheckpoint failed; solver stopped");
        return;
    }

    g.savedPoints.push_back({0, rootCheckpoint});

    if (!g.hud && Mod::get()->getSettingValue<bool>("show-hud")) {
        g.hud = cocos2d::CCLabelBMFont::create("QFS: starting", "bigFont.fnt");
        if (g.hud) {
            g.hud->setScale(0.35f);
            g.hud->setAnchorPoint({0.0f, 1.0f});
            auto h = cocos2d::CCDirector::sharedDirector()->getWinSize().height;
            g.hud->setPosition({8.0f, h - 8.0f});
            pl->addChild(g.hud, 10000);
        }
    }
    log::info("QFS: initialized practice checkpoint; interval={} simulation steps", g.planner.interval);
    updateHUD(pl, true);
}
} // namespace

class $modify(QuarterFramePlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        g = Runtime{};
        g.owner = this;
        return true;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (this != g.owner) return;
        if (!settingEnabled() || !this->m_isPracticeMode) {
            if (g.active) {
                qfsReleaseJump(this);
                g.active = false;
            }
            return;
        }
        if (!g.active && !g.stopped && !g.won && !this->m_playerDied &&
            !this->m_hasCompletedLevel && !this->m_isPaused) {
            start(this);
            return;
        }
        if (!usable(this) || g.deathPending || this->m_playerDied) return;

        if (g.planner.needsCheckpoint()) {
            qfsReleaseJump(this); // snapshot only after jump has been released
            if (auto* newlySaved = this->markCheckpoint()) {
                g.planner.savedCheckpoint();
                g.savedPoints.push_back({g.planner.checkpoint, newlySaved});
                g.pressesThisAttempt = 0;
                updateHUD(this, true);
            } else {
                g.stopped = true;
                g.stopReason = "checkpoint save failed";
                qfsReleaseJump(this);
                log::warn("QFS: failed to save practice checkpoint");
            }
        } else updateHUD(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // Capture the failing frame BEFORE GD starts its death/reset flow.
        if (usable(this) && !g.deathPending && !g.planner.exhausted) {
            g.deathPending = true;
            const auto failed = g.planner.tick;
            ++g.deaths;
            g.lastDeathTick = failed;
            const bool diedBeforeFirstPress =
                g.pressesThisAttempt == 0 && failed <= g.planner.checkpoint + 1;
            const bool canRetryHere =
                !diedBeforeFirstPress && g.planner.onDeath(failed);

            if (canRetryHere) {
                log::info("QFS: died at tick {}; retry {} with click at {}",
                    failed, g.planner.retries, g.planner.clickAt);
            } else if (g.savedPoints.size() > 1) {
                // The current checkpoint is too late for any remaining input.
                // Undo it BEFORE Geometry Dash chooses its respawn point.
                g.backtrackPending = true;
                log::info("QFS: checkpoint boundary reached at {}; will backtrack",
                    g.planner.checkpoint);
            } else {
                g.stopped = true;
                g.stopReason = diedBeforeFirstPress ? "died before first click at start" :
                    "search exhausted at level start";
                log::warn("QFS stopped at earliest checkpoint, death tick {}", failed);
            }
            updateHUD(this, true);
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() {
        const bool automaticRetry = (g.owner == this && g.deathPending);
        bool backtrackOK = false;
        qfs::RetryPlanner::Tick earlierTick = 0;
        qfs::RetryPlanner::Tick discardedTick = 0;

        if (automaticRetry && g.backtrackPending && g.savedPoints.size() > 1) {
            const auto newest = g.savedPoints.back();
            const auto previous = g.savedPoints[g.savedPoints.size() - 2];
            earlierTick = previous.tick;
            discardedTick = newest.tick;

            // Never delete a user's manually inserted checkpoint by accident.
            if (this->getLastCheckpoint() == newest.handle) {
                this->removeCheckpoint(false); // GD 2.2081: newest checkpoint
                backtrackOK = this->getLastCheckpoint() == previous.handle;
            }
            if (backtrackOK) {
                g.savedPoints.pop_back();
                ++g.backtracks;
                log::info("QFS: removed checkpoint at {}; restoring {}",
                    discardedTick, earlierTick);
            } else {
                g.stopped = true;
                g.stopReason = "checkpoint removal mismatch";
                log::warn("QFS: removal did not reveal previous checkpoint; stopping safely");
            }
        }

        // Important: GD chooses the latest remaining checkpoint during reset.
        PlayLayer::resetLevel();
        if (g.owner != this) return;
        qfsReleaseJump(this);

        if (automaticRetry) {
            if (g.backtrackPending && backtrackOK) {
                if (!g.planner.backtrackTo(earlierTick, discardedTick)) {
                    g.stopped = true;
                    g.stopReason = "no frame between checkpoints";
                }
            } else if (!g.backtrackPending) {
                g.planner.rewindToCheckpoint();
            }
            g.pressesThisAttempt = 0;
            g.deathPending = false;
            g.backtrackPending = false;
            updateHUD(this, true);
        } else {
            // Manual restart discards old checkpoint bookkeeping.
            g.active = false;
            g.deathPending = false;
            g.backtrackPending = false;
            g.savedPoints.clear();
            g.stopped = false;
            g.won = false;
        }
    }

    void levelComplete() {
        if (g.owner == this && g.active) {
            g.won = true;
            g.active = false;
            qfsReleaseJump(this);
            log::info("QFS: level complete in practice mode");
            updateHUD(this, true);
        }
        PlayLayer::levelComplete();
    }

    void onQuit() {
        if (g.owner == this) {
            qfsReleaseJump(this);
            g = Runtime{};
        }
        PlayLayer::onQuit();
    }
};

// Count calls into GD's command-processing step, rather than counting display
// frames. This is the input seam used by playback mods in 2.2081.
class $modify(QuarterFrameCommands, GJBaseGameLayer) {
    void processCommands(float dt, bool arg1, bool arg2) {
        if (auto* pl = PlayLayer::get(); pl && static_cast<GJBaseGameLayer*>(pl) == this &&
            usable(pl) && !g.deathPending && !pl->m_playerDied) {
            g.planner.advanceStep();
            // Hold a jump for exactly one command-processing step.
            if (g.pressed && g.planner.tick > g.planner.clickAt)
                qfsReleaseJump(pl);
            if (g.planner.shouldPress()) {
                pl->handleButton(true, 1, true);
                g.pressed = true;
                ++g.pressesThisAttempt;
                ++g.totalPresses;
                log::info("QFS: jump PRESSED at tick {} (checkpoint {}, total presses {})",
                    g.planner.tick, g.planner.checkpoint, g.totalPresses);
            }
        }
        GJBaseGameLayer::processCommands(dt, arg1, arg2);
    }
};

// Fast path: multiple fixed-delta engine updates per render. GD draws once
// after scheduler returns. This is experimental and deliberately capped.
// Does NOT skip simulation/collision updates or render-level scripts.
class $modify(QuarterFrameScheduler, CCScheduler) {
    void update(float dt) {
        PlayLayer* pl = PlayLayer::get();
        if (usable(pl) && !g.deathPending && stepsPerRender() > 1) {
            const int steps = stepsPerRender();
            const float fixedDt = 1.0f / static_cast<float>(ticksPerSecond());
            for (int i = 0; i < steps; ++i) {
                CCScheduler::update(fixedDt);
                if (!usable(PlayLayer::get()) || g.deathPending) break;
            }
        } else {
            CCScheduler::update(dt);
        }
    }
};

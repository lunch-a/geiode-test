#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include "RetryPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

using namespace geode::prelude;

namespace {
struct Runtime {
    PlayLayer* owner = nullptr;
    cocos2d::CCLabelBMFont* hud = nullptr; // Owned by the PlayLayer scene
    qfs::RetryPlanner planner;
    bool active = false;
    bool deathPending = false;
    bool pressed = false;
    bool won = false;
    bool stopped = false;
    int lastHudTick = -1;
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
    else if (g.stopped) message = "QFS: stopped (search exhausted)";
    else message = "QFS CP=" + std::to_string(g.planner.checkpoint) +
        " tick=" + std::to_string(g.planner.tick) +
        " click=" + std::to_string(g.planner.clickAt) +
        " retry=" + std::to_string(g.planner.retries);
    g.hud->setString(message.c_str());
}

void start(PlayLayer* pl) {
    g.active = true;
    g.deathPending = false;
    g.stopped = false;
    g.won = false;
    g.pressed = false;
    g.planner.begin(0, std::max(1, (ticksPerSecond() + 2) / 4), retryLimit());

    // Start with a real GD checkpoint, not a fake saved x/y position. This
    // also saves triggers, game state, and other information that GD tracks.
    if (pl->markCheckpoint() == nullptr) {
        g.active = false;
        g.stopped = true;
        log::warn("QFS: markCheckpoint failed; solver stopped");
        return;
    }

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
            if (this->markCheckpoint() != nullptr) {
                g.planner.savedCheckpoint();
                updateHUD(this, true);
            } else {
                g.stopped = true;
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
            const bool canTryAgain = g.planner.onDeath(failed);
            if (!canTryAgain) {
                g.stopped = true;
                log::warn("QFS: no earlier one-tick click remains near checkpoint {}",
                    g.planner.checkpoint);
            } else {
                log::info("QFS: died at tick {}; retry {} with jump on {}",
                    failed, g.planner.retries, g.planner.clickAt);
            }
            updateHUD(this, true);
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void resetLevel() {
        const bool automaticRetry = (g.owner == this && g.deathPending);
        PlayLayer::resetLevel();
        if (g.owner != this) return;
        // GD has now restored its actual practice checkpoint state.
        // Restore the mod's frame counter to the corresponding logical tick.
        qfsReleaseJump(this);
        if (automaticRetry) {
            g.planner.rewindToCheckpoint();
            g.deathPending = false;
            updateHUD(this, true);
        } else {
            // Manual restart: discard our old search and start fresh.
            g.active = false;
            g.deathPending = false;
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

#include "../src/RetryPlanner.hpp"
#include <cassert>
#include <iostream>

int main() {
    qfs::RetryPlanner p;
    p.begin(0, 60, 100);
    assert(p.clickAt == 1 && p.nextCheckpoint == 60);
    for (int i = 0; i < 60; ++i) p.advanceStep();
    assert(p.needsCheckpoint());
    p.savedCheckpoint();
    assert(p.checkpoint == 60 && p.clickAt == 61);
    // Initial +1 click failed on frame 83 => try 82 after restoring.
    p.tick = 83;
    assert(p.onDeath(p.tick) && p.clickAt == 82);
    p.rewindToCheckpoint();
    assert(p.tick == 60);
    p.tick = 84;
    assert(p.onDeath(p.tick) && p.clickAt == 81);
    p.rewindToCheckpoint();
    // If we die before the click, jump directly to before that death.
    p.tick = 73;
    assert(p.onDeath(p.tick) && p.clickAt == 72);
    for (int i = 0; i < 20; ++i) {
        p.tick = p.clickAt + 1;
        const bool canTry = p.onDeath(p.tick);
        if (!canTry) break;
    }
    assert(p.exhausted);
    qfs::RetryPlanner low;
    low.begin(7, 1, 2);
    assert(low.clickAt == 8);
    assert(!low.onDeath(8));
    assert(low.exhausted);
    qfs::RetryPlanner limit;
    limit.begin(0, 60, 1);
    assert(limit.onDeath(50));
    assert(!limit.onDeath(51));
    std::cout << "RetryPlanner tests passed\n";
}

# Quarter Frame Solver — experimental Geode mod

A **source-code prototype**, not a compiled or verified `.geode` release. Intended for **Geometry Dash 2.2081 on Windows** and Geode **5.10.1** bindings. The exact SDK and game version need to match your installation; adjust `mod.json` to your installed SDK/version where necessary.

## What it implements

- Works only in **Practice Mode**, and is OFF by default.
- Uses GD's `PlayLayer::markCheckpoint()` and ordinary practice checkpoint restoration, instead of simply resetting player position.
- Adds checkpoints every **round(F / 4)** counted game command-processing ticks, where F is configured simulation ticks/second. E.g. F=240 -> 60-tick intervals.
- After reaching each checkpoint, presses jump on the next counted tick, and releases it on the following tick (one-tick tap).
- On death at tick `D`, retries the section from the latest checkpoint with the click on `D - 1`.
- On subsequent deaths moves the candidate click earlier, never later: `min(previous_click - 1, new_death - 1)`.
- Halts the solver (not the game) if it reaches the checkpoint or the retry limit without success.
- Displays checkpoint, frame, candidate click, and retry count in a HUD.
- Optional *steps per render* acceleration: feeds multiple fixed-delta engine scheduler updates into one rendered frame, capped at 32. Default 1 (safe baseline).

## Major limitations (important)

1. **This is NOT a guaranteed level solver.** A one-frame tap and a search that only moves earlier cannot solve levels requiring multiple inputs, long holds, releases, dual-player inputs, etc. It may exhaust a section without finding any successful tap. A real general solver would search input sequences, branches, and held durations.
2. This is a **research prototype**. The Geode SDK is NOT installed in the authoring environment, so it has **not been compiled against Geode or play-tested**. The standalone retry-state planner is compiled and unit tested.
3. Depending on GD's internal command-processing order, **frame labels or input injection may require a one-tick calibration**. Disable in-game Click Between Frames and other replay/TPS/speedhack mods while debugging.
4. `PlayLayer::markCheckpoint()` may not be safe at every spot in custom levels; checkpoint restore can behave differently with persistent triggers, RNG, or unusual level features.
5. The high-speed scheduler option can cause timing glitches and may affect **audio, timers, other Geode mods and performance**. Start at 1. Test 2 then 4, then 8.
6. The solver deliberately operates in **Practice Mode**. It does not turn a practice clear into a legitimate normal-mode clear or submit scores.
7. Initially targets ordinary single-player, auto-scrolling levels. Platformer levels and dual mode are not supported correctly.
8. Internal physics tick rate and graphics refresh rate can differ. `F` here is the **desired physics/command tick rate**, not automatically your monitor's Hz. If the game processes multiple internal substeps per update, the mapping must be verified.

## Build on Windows

A GitHub Actions build workflow is included at `.github/workflows/build-windows.yml`; see `BUILD_WINDOWS.md` for steps and how to retrieve a successful `.geode` artifact. **This source has not been compiled against the full SDK here.**

### Local build steps

1. Install the [Geode SDK + CLI](https://docs.geode-sdk.org/getting-started/) for your installed GD version, LLVM/Clang, CMake, and Ninja / build tools.
2. Put this folder in your projects directory.
3. Change `mod.json`'s `geode` and `gd.win` version fields as required by the SDK and game you're actually using.
4. Open a PowerShell terminal from this folder:

   ```powershell
   geode sdk install
   geode sdk install-binaries
   geode build
   ```

5. Find the generated `.geode` package in the build output (or installed mod folder, according to your Geode CLI configuration) and load it in Geode.
6. Enable **Quarter Frame Solver** in its settings, turn on Geometry Dash **Practice Mode**, restart the level. Start with **Steps per render = 1** and **F = 240**.

If compilation fails because a binding moved in your installed Geode version, consult the generated bindings for `GJBaseGameLayer::processCommands`, `PlayLayer::markCheckpoint`, `PlayLayer::postUpdate`, and `CCScheduler::update`.

## Test the pure C++ retry planner separately

Linux/macOS:

```sh
c++ -std=c++17 -Wall -Wextra -pedantic tests/test_planner.cpp -o planner_test
./planner_test
```

Windows Developer Command Prompt:

```bat
cl /std:c++17 /EHsc /I src tests\test_planner.cpp
```

## Practical next version

After verifying which callbacks correspond to one physics tick in your build, extend the search to multiple press/release durations and preserve successful input sequences between checkpoints. Add sandboxed fast-forward/restoration and an explicit cancel button before using large speed multipliers.

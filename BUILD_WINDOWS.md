# Build Quarter Frame Solver for Windows

**Status:** C++ retry-planner tests passed locally. The full Geode mod has **not** been compiled against a Geode SDK or tested in Geometry Dash. This archive is *source*, not a ready-to-install `.geode` mod.

## Option 1: GitHub Actions (no local compiler required)

1. Create an empty GitHub repository.
2. Upload the **contents** of the `QuarterFrameSolver` directory to the repository root, including the hidden `.github/workflows/build-windows.yml` file. (For many files, using GitHub Desktop is easier than web uploads.)
3. Open the repository's **Actions** tab, select **Build Quarter Frame Solver (Windows)**, and choose **Run workflow**. The workflow also runs on push.
4. Open the resulting workflow run. If all build steps succeed, download the **QuarterFrameSolver-Windows** artifact.
5. Extract the downloaded artifact and install `local.quarterframesolver.geode` through Geode.
6. If the build fails, open the failed step's logs. Compiler errors mean the source needs fixing for the exact SDK/bindings; no `.geode` was produced.

This workflow uses `geode-sdk/build-geode-mod@main` on a Windows runner and matches the required Geode release in `mod.json`.

## Option 2: Build locally on Windows

Install the current Geode CLI and SDK using the official guide: https://docs.geode-sdk.org/getting-started/

Then, in the project directory (PowerShell):

```powershell
geode sdk install
geode sdk install-binaries
geode build --ninja
```

Find the `.geode` file in the build output, if the command succeeds. Keep the mod disabled except when testing Practice Mode.

## Logic tests (no Geode dependencies)

```sh
c++ -std=c++17 -Wall -Wextra -pedantic tests/test_planner.cpp -o planner_test
./planner_test
```

**Important:** This is a prototype and may fail to compile or behave incorrectly at runtime; it has not been tested against the game. The fast-forward option can conflict with game timing, and moving a jump progressively earlier is not a general solver.

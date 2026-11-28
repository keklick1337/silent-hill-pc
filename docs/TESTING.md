# Tests

Code: `tests/framework/`, `tests/CMakeLists.txt`, `tests/<module>/`, `tests/replay/`, `tests/smoke/`. The full gate before a merge (build, `sh_tests`, purity metrics, replay, map sweep, event scenarios, smoke) — [DEVELOPMENT.md](DEVELOPMENT.md), section 4.

## Running

From the project root:

```bash
cmake -S . -B build -G Ninja
cmake --build build --target sh_tests
build/tests/sh_tests                 # all cases
build/tests/sh_tests disc            # only cases whose name contains "disc"
ctest --test-dir build --output-on-failure
ctest --test-dir build -L replay     # replay harness: replay_tools and replay_check (needs the image or an import)
```

`sh_tests` is rebuilt together with the project. A new `*_test.cpp` file is picked up by the next `cmake --build` without running CMake by hand.

Output: `RUN  name` before each case, then its result — `PASS name`, `FAIL name` (below it, the file, line and expression of each failure), `SKIP name (reason)`. Each line is flushed at once, so a case that crashes the runner is the last `RUN` line of the log. At the end — `N passed, M failed, K skipped`.

Exit code:

- `0` — no FAIL (PASS and SKIP are allowed);
- `1` — at least one FAIL;
- `2` — wrong arguments or the filter matched no case.

`ctest` runs `sh_tests` as a whole, as one item, and the replay harness tests with the `replay` label; `replay_check` without the image is skipped (code 77). A selective run of `sh_tests` is done through the argument.

## Writing a test

File: `tests/<module>/<name>_test.cpp`, where `<module>` is the engine module name (`disc`, `vfs`, `mods`, `script`, `world`, `chara`, `render` and so on, see `tests/`). Files are searched recursively inside `tests/`. The file needs no `main`, it is in `framework/main.cpp`.

```cpp
#include "framework/sh_test.h"

#include <filesystem>
#include <string>

SH_TEST(disc_image_has_expected_size) {
    const std::string image = std::string(SH_PROJECT_ROOT) + "/data/Silent Hill (v1.1).bin";
    if (!std::filesystem::exists(image)) {
        SH_SKIP("no disc image in data/");
    }
    SH_CHECK(std::filesystem::file_size(image) == 616494480);
}
```

Macros (`tests/framework/sh_test.h`):

- `SH_TEST(name)` — declares a case. The name is a C++ identifier, not a string; the filter works on it.
- `SH_CHECK(expression)` — on a false value records a failure and continues the case, so one run shows several problems.
- `SH_SKIP("reason")` — aborts the case and marks it SKIP. If there was already a failure before it, the case is FAIL: a real failure matters more than a skip.
- An exception thrown from a case is FAIL with the `what()` text.
- A crash (segfault, abort) stops all of `sh_tests`: the following cases do not run, ctest shows FAIL.

Rules:

- The disc image (`data/Silent Hill (v1.1).bin`) is not required for the build. A test that needs the image checks that it exists and does `SH_SKIP` instead of failing. The image is not copied into the repository and is not printed whole.
- Paths are built from `SH_PROJECT_ROOT` — the project root, which CMake sets as a quoted string. Do not rely on the current directory: ctest runs `sh_tests` from `build/`.
- Cases must not depend on each other or on the order of execution.
- A test does not write into `data/`, the repository's `gamedata/` or `references/`. Temporary files go into `.tmp/<name>/`.
- Files in `tests/<module>/` are built only if the target `sh_<module>` exists; otherwise CMake prints a message at configure time and skips the file. The libraries `sh_tests` links with are listed in `tests/CMakeLists.txt`; code built only into the game (part of `src/engine/scripting`, `src/engine/chara/game`, `src/i18n`) is included by the test as a source file — also there. Headers are included as from `src/`: `#include "formats/disc/disc_image.h"`.
- Tests, like `src/`, hold no code of the original or of the decompilation: a test contains no "reference model" — a second implementation of a rule that the engine is compared with on the fly. Behaviour is pinned by a recorded table (below) and by a few tests that state the rule in words ("a paused frame skips the simulation step but is drawn").

### Recorded tables

`tests/data/<module>/<name>.txt` — observations of behaviour: inputs and the outputs the engine gives for them. Format (`tests/framework/golden.h`): a line is cells separated by spaces, the first cell is the row kind, `#` starts a comment; the file header describes the columns with engine names. `sh::test::golden_rows(file, kind)` returns the rows of one kind, the test feeds the inputs to the engine and compares; on a mismatch `golden_mismatch` prints the file, the table row and what the engine gave.

- A small input space is recorded in full (all flag combinations, all state pairs).
- A rule with memory is recorded as a run: rows in order, one object for the whole run.
- A long random run is recorded as a digest: the inputs come from `sh::test::Sequence` (the same series on any platform; each draw is a separate statement so that the order does not depend on the compiler), the outputs are folded by `sh::test::Digest`, and the table has a row `digest <seed> <steps> <digest>`. A digest catches any divergence but does not explain it — so explicit rows and tests in words always sit beside it.
- A table changes only together with an intended change of behaviour, in the same commit and with an explanation: the new values are taken from the output of the failed test (it prints the row and the engine's result), not from the old code.

| Table | What is recorded |
| --- | --- |
| `gameflow/main_frame.txt` | a main-loop frame over a stub game: the first 24 frames call by call, digests of 20 runs of 20000 frames |
| `gameflow/boot_machine.txt` | one boot step from each starting position: host calls and the result |
| `gameflow/scene_owner.txt` | ownership of the scene and the frame for all state and flag pairs; a run of the world anchor; a digest of a million steps |
| `gameflow/freeze_latch.txt`, `packet_arena.txt` | runs of the freeze-frame latch and the packet arena monitor, digests of long runs |
| `gameflow/clear_color.txt`, `frame_cheats.txt`, `debug_camera.txt`, `soft_reset.txt` | all input combinations |
| `gameflow/load_screen_view.txt` | loading screen camera targets by position and rotation |
| `effects/sfx_mix.txt` | a run of panning and volume of a positional sound |
| `world/bone_stream.txt` | reading of bone model streams |
| `chara/footsteps.txt` | footstep sound by map and material, digests by room and special places |
| `screens/map_screen.txt` | page links, snap grids, sprites and markers of `gamedata/menu/map_screen.json` |

## Smoke test

`tests/smoke/smoke.sh` starts the game, one second before the end of the limit takes a frame by the engine itself (the console command `shot`) and, if needed, compares it with a reference. Video, size and screenshots follow the common standard.

```bash
tests/smoke/smoke.sh [-r WxH] [-v auto|offscreen|xvfb] [-a game-argument]... <binary> <work dir> <seconds> <out.png> [reference.png] [threshold]
```

- Video — `SDL_VIDEODRIVER=offscreen` (real GPU, no window); Xvfb on `:290`–`:299` (or `SH_XVFB_DISPLAYS`) is only a fallback: `-v xvfb`, `SH_VIDEO=xvfb` or if SDL did not start under offscreen.
- Size — `-r`, `1920x1080` by default. The game reads `config.cfg` beside the exe, so the run goes from `<work dir>/smoke-run/`: a hard link (or copy) of the exe, links to the rest of its directory and a copy of `config.cfg` with `width`/`height`/`fullscreen` replaced. If `<binary>` is a wrapper script, the size comes from its config; instead of a wrapper with `-skiptogame` use `-a -skiptogame`.
- The game runs under `timeout <seconds>` with `-consolecmd`: `@<seconds-1> SHOT <out.png>`. Time is wall-clock; the shot fires during intros and videos too. Code `124` from `timeout` is normal: the game ran for the whole limit. A line of the `-consolecmd` file and everything the command printed also go into the log (category `console`, level info): this way a run without eyes reads the answers of `spawn`, `charas` and other commands.
- The prefix `@+N` in the `-consolecmd` file waits N sixtieths of a second of game time (update steps while the player controls Harry) after the previous command: the command lands on the same update at any frame rate. A change of the update step (a stalled frame) is logged as a line `[CONSOLECMD] step A -> B`.
- **Quit during the demo:** `tests/smoke/quit_in_demo.sh <binary> <work dir> [seconds]` — the game with imported data stays on the title screen until the demo (`skip_intros = 1`), at the given second (45 by default) `quit` runs; it checks exit code 0, that the demo recording (`MISC/PLAY*.DAT`) was read before the exit, that no read went past its buffer and that there is no `[CRASH]` report.
- **Import without starting the game:** `tests/smoke/quit_after_import.sh <binary> <image.cue> <work dir>` — `--quit-after-import` without the image exits with code 1, with the image imports into its own directory (`-data`) and exits with code 0, a repeated run on the ready directory also gives 0; the game never starts.
- **Frame-rate independence**: `python3 -I tests/smoke/fps_ab.py --build <build> --gamedata <gamedata> --out .tmp/fps_ab [--enemy Groaner] [--health 200] [--caps 30,60,120,240,0] [--frames] [--style classic|tps|fps] [--map map1_s00]` — the same steel-pipe fight in the school (fixed RNG seed, hits on a game-time schedule with the `pad` command) at several `fps_cap` values; the rows for 60 fps and above must match (hits, enemy and Harry health, hash of positions, states and the random seed at each mark), 30 fps runs its own 30 Hz step. `--style tps` and `--style fps` play it with the Third Person or First Person camera and turn the view with a scripted right stick (`lookinput stick`): the frames between two updates turn the picture on, which must not reach the game (the weather spawns and draws its random numbers around the view of the update). `--stall <percent>` enables artificial update delays (`stall`): the traces must match a run without delays at the same `fps_cap`, including 30. The `stalled frames` column counts step changes after the seed (must be 0: delays are caught up by updates of the normal step). `--frames` saves each shown frame of the turn and the hit (`shotseq`).
- Logs beside the shot: `<out>.game.log` (the game's stdout and stderr) and `<out>.xvfb.log`.
- The reference is compared with `compare -alpha off -metric AE` (RGB only). The threshold is the number of differing pixels, `0` by default. The reference must have the same size as `-r`. The shot is taken by the clock, so the frame can differ slightly from run to run; then a threshold above zero is needed.

Exit codes:

| Code | Meaning |
| --- | --- |
| `0` | the game ran for the whole limit, the shot is saved; it matched the reference, if one is given |
| `1` | the shot differs from the reference by more than the threshold |
| `2` | error: arguments, files, no free display, Xvfb or `compare` failure, no shot taken; a reference of another size |
| `3` | the game exited before the limit, its code is in the message |
| `77` | offscreen did not start and there is no `Xvfb` (or `-v xvfb` without `Xvfb`), the test is skipped |

Needed: `timeout`, for the reference also `compare` and `identify` (ImageMagick), for the fallback `Xvfb`.

The smoke test is not part of `ctest`: it needs a built game and a reference.

## Replay harness

The harness guards against regressions: a change that touches logic is accepted only if `replay_check` passes, or if the dump change is explained and the reference updated in the same commit. It does not prove fidelity to the console; it proves the build behaves as before: two runs are byte-identical, and a divergence from the reference is shown by tick and field.

```sh
python3 -I tools/replay/sh_replay.py check --build build            # every route, twice, against its reference
python3 -I tools/replay/sh_replay.py check --build build newgame --runs 5 --stress
python3 -I tools/replay/sh_replay.py compare a.dump.gz b.dump.gz     # the first diverging tick and field
python3 -I tools/replay/sh_replay.py update-ref --build build <route> # after an explained change of behaviour
```

- **Routes** are recorded input, `tests/replay/routes/*.replay`; their references are `tests/replay/reference/<route>.dump.gz` (the full per-tick state dump, gzip with `mtime = 0`, so the same dump gives the same bytes). New routes come from recording a play session or from `tools/replay/gen_route.py` (seeded random input).
- **Replay mode** is switched on by environment variables that `sh_replay.py` sets; without them the game behaves normally. Time is virtual: a run's result does not depend on the machine's speed.
- **Imported data** is required in `<build>/gamedata/`; without it `sh_replay.py` imports once, looking for the image in `SH_DISC_IMAGE`, `<build>/data/`, `<build>/` and the project's `data/`. Without an image `replay_check` is skipped (code 77).
- **Exit codes** of `sh_replay.py`: 0 everything matched; 1 dumps differ or a run log has an error line; 2 the game did not start or did not reach the route's end; 77 skipped.
- **Video**: `SDL_VIDEODRIVER=offscreen` by default (the real GPU, no window); `--video xvfb` uses a private X server.
- **Map scenarios** (`tools/replay/handler_diff.py`, next section) are the same idea per map: the auto-scenario of each map and the scenario of each event handler, references in `tests/replay/handlers/`.

## Map sweep and scenarios

Every map is checked in two ways: the map sweep loads each of the 43 maps and walks in it, and the map scenarios run every event handler of every map and compare the result with a recorded reference.

- `tests/smoke/map_sweep.sh [-t seconds] [-j games] [-d gamedata] [-g] [-x key=value] <build> <image.bin> <out> [maps…]` — each of the 43 maps from a new game, simple walking, two shots; the result is `<out>/summary.tsv` (`alive`, `static`, `fatal`, `dialog`, `crash:<signal>`, `exit:<code>`). `-j` — up to 10 games in parallel. Without `-d` the image is imported once into `<out>/gamedata`; if `<build>/gamedata` already exists, pass `-d`.
- `tools/replay/handler_diff.py run --build <build> -j 8 --repeat [maps…]` — regression of map scenarios: the auto-scenario of each map, the scenario of each event handler slot and the scenarios of `tools/replay/*_scenarios.json` are compared with the references `tests/replay/handlers/<map>.json`; an error is a divergence from the reference, a scenario without a reference, two runs of one scenario not being equal (`--repeat`), an error line in the log or a handler Lua error. The references are written by `handler_diff.py update-ref`.
- Data shape checks without the game: `tools/scripts/check_map_handlers.py` (`handlers.json` of all maps), `gen_map_tables.py --check` (`tables.json`), `gen_text.py --check` (`gamedata/text/`), `gen_flags.py --check`, `gen_paper_maps.py --check`, `gen_weapon_rig.py`, `check_naming.py`; with an import — `check_anim_clips.py <dir>/tables/anim` (ANM clips; a character whose behaviour calls `anim_step` has `clip_tables`, and each named table exists), `gen_map_tables.py --check-exe-owned <dir>`.
- Tests with the game run without sound: `ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy`.

## What the tests do not check

- Input from real devices, sound by ear, saves live. Smoke only starts the game and takes a screenshot; the replay routes are `map0_s00` (three), the school, the hospital, the sewers, the hospital's Otherworld, the boss fight.
- Timeouts of individual cases: a hung case hangs all of `sh_tests`.
- Process isolation of cases: a crashing case breaks off the run.

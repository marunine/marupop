<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Development tools

Programs and scripts that support development. None of them is installed, and none is part of a
release.

## Policy

Probes exercise platform paths without the full application. Drivers run individual
subsystems against explicit inputs for diagnostics and reproducible measurements. Record
commands, input descriptions, build settings and dependency versions when reporting timings.

Every target is defined in every configure, so a single tool can be built by name at any time:

```
cmake --build build --target marupop-<name>
```

`-DMARUPOP_BUILD_DEV_TOOLS=ON` adds every one of them to the default target, which is how a
build proves they still compile against the current library.

## The KWin privilege gate

The capture path calls `org.kde.KWin.ScreenShot2`, which KWin grants only to a process whose
`/proc/pid/exe` matches the `Exec` of an installed desktop entry declaring that interface. A
binary in a build tree has no such entry.

| Command | Effect |
| --- | --- |
| `tools/install-dev-desktop.sh build` | Writes entries for `build/bin/marupop`, `build/bin/marupop-captureprobe`, `build/bin/marupop-hoverprobe` and `build/bin/framesource_test` into `~/.local/share/applications` and runs `kbuildsycoca6`. The application, capture probe and frame-source test declare `org.kde.KWin.ScreenShot2`; the hoverprobe entry declares `org_kde_kwin_fake_input` instead, which is the global its pointer warp binds. The application entry is named `io.github.marunine.marupop.desktop` and shadows an installed entry. |
| `tools/install-dev-desktop.sh --uninstall` | Removes those entries. Leaves the installed entry as the only match. |

## The tools

| Tool | What it does |
| --- | --- |
| `install-dev-desktop.sh` | Writes and removes the build-tree desktop entries described above. |
| `authcheck` | Prints, for each executable path given, the desktop entry KWin would match and the `X-KDE-*` interfaces it would read. Exit code 0 where every path is authorized. With no argument it checks the installed `marupop` and the one beside itself. |
| `captureprobe` | Times `org.kde.KWin.ScreenShot2` through `maru::capture::KWinGrabber` over eight region sizes and prints min/p50/p90/max per case, plus the XXH3-64 cost. `marupop-captureprobe X Y W H N [INTERVAL]` runs one `CaptureArea` case instead. Needs the desktop entry above. |
| `cursorprobe` | Runs the `maru::cursor::KWinScriptRelay` bootstrap against the live compositor and prints the delivery rate and the one-way latency of the relay. `marupop-cursorprobe [SECONDS] [--idle]`. Needs the bus name `io.github.marunine.marupop` to be free, so a running MaruPop has to be quit first. It unloads the script on exit and leaves `~/.local/share/kwin/scripts/marupopcursor/` and the `kwinrc` key in place, which is the state the application installs. |
| `popupprobe` | Shows one sample `maru::popup::PopupWindow` beside the pointer on the live session for five seconds, which is the only way to see the layer-shell surface, the margins against the target screen's origin and the pitch-accent overlay. `marupop-popupprobe [--at X,Y] [--pinned] [--seconds N] [--render FILE]`. `--at` supplies a pointer position, since Qt reports `QCursor::pos()` as (0,0) on Wayland while the process has no pointer focus; `--render` writes the card to a PNG instead of mapping a surface, which needs no compositor and no screen grab. Needs no desktop entry. |
| `hoverprobe` | Paints Japanese text on a layer surface at a chosen logical position and warps the pointer onto a named character through `org_kde_kwin_fake_input`, which is what makes the whole pointer, grab, recognition, lookup and popup chain scriptable. `marupop-hoverprobe [--at X,Y] [--hover LINE:INDEX]… [--hover-delay MS] [--sweep X1,Y1:X2,Y2]… [--sweep-step PX] [--sweep-interval MS] [--away X,Y] [--seconds N] [--pixel-size PX] [--foreground COLOUR] [--background COLOUR] [line…]`. `--hover` holds the pointer still for as long as `--hover-delay`; `--sweep` moves it along a straight line, one warp of `--sweep-step` logical pixels every `--sweep-interval` milliseconds, defaulting to the 8 ms pump the cursor relay runs at, and prints a `track X,Y at <epoch ms>` line per warp so a log of the process under test can be read against the pointer's own track. Without either it prints the character rectangles and waits. `--pixel-size`, `--foreground` and `--background` replace the 28 pt dark-on-light default; set them explicitly to reproduce a particular glyph size and contrast. It needs the desktop entry above, and the target it builds at all needs plasma-wayland-protocols, wayland-scanner and wayland-client at configure time. |
| `dictuiprobe` | Opens the Manage Dictionaries dialog against a scratch dictionary directory. `marupop-dictuiprobe [--seed] [--import] [--render DIR] [source…]`, where a source is a Yomitan folder or zip added to the list and `--render` writes the dialog and each sub-dialog to PNG files in DIR and exits. `MARUPOP_DICTUI_HOME` selects the directory. The drag reordering, the icon-theme decorations and the message widget's action buttons are only observable on a real session, which the offscreen test platform is not. |
| `importprobe` | Drives `maru::dict::DictionaryManager` from a terminal: it seeds the built-in entries, points one at a local dump, adds a Yomitan folder or zip, and runs the import job to completion with the wall time and the resulting file sizes. `marupop-importprobe --list`, `--models`, `--seed`, `--set-source <name> <path>`, `--add-yomitan <path> [--as <type>]`, `--import <name-or-id>`. `MARUPOP_DICT_HOME` points a run at a scratch copy. |
| `lookupprobe` | Times `lookup::Engine` against an existing dictionary directory. `--dir PATH` selects the directory; `--text FILE` supplies UTF-8 text; `--stages` and `--scale` report pipeline and dictionary-count costs. `--ocr-variants` enables character variants, and `--substitutable-share X` supplies synthetic confidence eligibility. `--pairs FILE` replays aligned expected/read JSONL examples; `--baseline` measures reads with variants disabled. `MARUPOP_DICT_HOME` also selects the dictionary directory. Use `--help` for all options. No compositor is required. Synthetic examples demonstrate behavior and do not estimate real-world accuracy. |
| `gen-jp-tables.py` | Regenerates `src/jp/japanesetables.h` from a supplied public JL checkout. |
| `gen-meiki-corrections.py` | Regenerates `src/ocr/meikicorrectiontable.h` from the repository correction rules. |
| `gen-meiki-correction-cases.py` | Regenerates synthetic correction cases using the standalone Python reference interpreter. |
| `gen-variant-eval-corpus.py` | Writes original synthetic aligned expected/read examples for `lookupprobe --pairs`. |
| `gen-similar-kanji.py` | Regenerates `src/lookup/similarkanjitable.h` from the supplied similar-kanji data file. |
| `gen-deconj-tests.py` | Regenerates `tests/deconj/deconjugation_cases.h` from a supplied public JL checkout. |

## Regenerating data

Normal builds use the checked-in outputs and require no source checkouts or Python generators.
Run the self-contained correction and synthetic evaluation generators from the repository:

```sh
python3 tools/gen-meiki-corrections.py
python3 tools/gen-meiki-correction-cases.py
python3 tools/gen-variant-eval-corpus.py --out /tmp/marupop-variants.jsonl
```

The first two accept optional positional input and output paths. Their default input is
`tools/data/meiki-corrections.json`; the fixture generator uses `tools/meiki_reference.py`.
The [rule specification](data/README.md) explains provenance and maintenance. Review new rules
with hand-justified expected behavior and run `ocr_meikicorrections_test` after regeneration.
Do not derive the expected fixture from the C++ implementation being tested.

The public-input generators require explicit paths:

```sh
python3 tools/gen-jp-tables.py /path/to/JL
python3 tools/gen-deconj-tests.py /path/to/JL
python3 tools/gen-similar-kanji.py /path/to/kanji.tgz_similars.ut8
```

Each accepts an optional output path as its final argument; the default is the repository's
tracked output. Use the public revisions and license notices in [NOTICE](../NOTICE),
`third_party/` and generated banners. Compare regenerated output before committing, and update
provenance together with intentional upstream-data changes. These paths are examples selected
by the contributor, not required workspace layouts.

ONNX Runtime providers are reported by `ocr::availableProviders()`. Set `MARUPOP_TRY_GPU=1`
and supply the models described in [TESTING.md](../docs/TESTING.md) to exercise providers in
`ocr_backend_test`. Report runtime, provider, model and hardware details with performance results.

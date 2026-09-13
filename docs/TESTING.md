<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Testing

## Run tests

Build with the [development configuration](../CONTRIBUTING.md#build-and-verify), then run:

```sh
ctest --test-dir build --output-on-failure -LE 'nested|hyprland'
```

Tests use isolated settings. D-Bus tests also need `dbus-run-session`.
Review skipped cases with verbose output:

```sh
ctest --test-dir build -V -R '<test-name>'
```

A passing CTest entry can contain skipped cases. Each skip message names the missing resource.

To run one case from an offscreen test binary:

```sh
QT_QPA_PLATFORM=offscreen ./build/bin/<name> --gtest_filter='<Suite>.<case>'
```

For a binary that uses fake D-Bus services, start a private bus:

```sh
env MARUPOP_TEST_PRIVATE_BUS=1 QT_QPA_PLATFORM=offscreen \
    dbus-run-session -- ./build/bin/<name> --gtest_filter='<Suite>.<case>'
```

Set `MARUPOP_TEST_PRIVATE_BUS=1` only with `dbus-run-session`.

## Compositor tests

KWin tests require `kwin_wayland`, `dbus-run-session` and working EGL rendering.
Capture cases also need a DRM render node. Without one, KWin composites with QPainter,
cancels every screenshot, and the capture cases skip.
Hyprland tests also require `Hyprland`.

```sh
ctest --test-dir build -L nested --output-on-failure
ctest --test-dir build -L hyprland --output-on-failure
```

The Hyprland harness tries a private KWin parent first.
If the private parent is unavailable, it may open a window in your Wayland session.
Use `--parent kwin` when running the harness to require a private parent:

```sh
tests/harness/hyprland-session.sh --parent kwin --home /tmp/hypr -- ./build/bin/hyprlandlive_test
```

To run a capture test inside a private KWin session:

```sh
tests/harness/nested-session.sh --home /tmp/nest --authorize "$PWD/build/bin/framesource_test" \
    -- env MARUPOP_LIVE_CAPTURE=1 ./build/bin/framesource_test
```

Harness exit code 77 means the compositor session was unavailable.
CTest reports that result as a skip.
Read the options in the [KWin harness](../tests/harness/nested-session.sh) or
[Hyprland harness](../tests/harness/hyprland-session.sh) for permissions and multiple outputs.

## Optional inputs

Set the variables below to enable tests that need external resources:

| Variable | Input or action |
| --- | --- |
| `MARUPOP_REAL_DATA=1` | Enable dictionary import and lookup benchmarks. |
| `MARUPOP_DICTIONARY_RESOURCES=<dir>` | Folder containing `JMdict.xml` and optional `PoS.json`. |
| `MARUPOP_YOMITAN_ROOT=<dir>` | Folder containing Yomitan dictionary folders. |
| `MARUPOP_JMDICT_PATH=<file>` | JMdict XML file for lookup benchmarks. |
| `MARUPOP_NETWORK=1` | Allow model tests to download from huggingface.co. |
| `MARUPOP_MEIKI_MODELS=<dir>` | Folder containing meikiocr ONNX models. |
| `MARUPOP_SCREEN_AI_RESOURCES=<dir>` | Chrome Screen AI component folder. |
| `MARUPOP_TRY_GPU=1` | Attempt GPU execution providers. |
| `MARUPOP_BENCH=1` | Enable the deconjugation benchmark. |

The [import benchmarks](../tests/dict/realdata_test.cpp) require both
`MARUPOP_DICTIONARY_RESOURCES` and `MARUPOP_YOMITAN_ROOT` to name existing folders.
Use these subfolders under `MARUPOP_YOMITAN_ROOT`:

| Folder | Dictionary |
| --- | --- |
| `grammar` | E de wakaru |
| `monolingual` | Sanseido eighth edition |
| `frequency` | JPDB kana frequency |
| `pitch` | NHK pitch accent |

Missing dictionaries skip their cases.
The [lookup benchmarks](../tests/lookup/realdata_test.cpp) use `MARUPOP_JMDICT_PATH`.

Install a Japanese font for the [OCR resolution tests](../tests/ocr/detectorresolution_test.cpp).
Record the font, inputs, command and environment when reporting benchmark results.

## Write a test

1. Add `tests/<module>/<subject>_test.cpp` under the module that matches the source.
2. Register the suite with a helper from [tests/CMakeLists.txt](../tests/CMakeLists.txt).
3. Reuse the [shared test helpers](../tests/support/).
4. Use `GTEST_SKIP()` when a required resource is missing. Name the resource and how to supply it.

Choose a helper for the environment the test needs:

| Need | CMake helper |
| --- | --- |
| Offscreen test | `maru_add_gtest` |
| Offscreen test with its own `main()` | `maru_add_gtest_app` |
| Fake D-Bus service | `maru_add_gtest_dbus` |
| KWin session | `maru_add_nested_test` |
| Hyprland session | `maru_add_hyprland_test` |

Write expected results independently of the code under test.
Use the [event-loop helpers](../tests/support/eventloop.h) when waiting for asynchronous results.

## Keyboard and screen-reader checks

Use an installed application and a synthetic Japanese sample.
Record the compositor and screen-reader versions with the results:

1. Enable scanning and point at the sample. Confirm the popup leaves focus in the previous application.
2. Press **Pin Popup** (Meta+Alt+P by default on Plasma). Confirm the result takes focus and stays pinned.
3. Use arrows, Page Up/Page Down and Home/End. Check that the result scrolls to show the selected position.
4. Select text with Shift and navigation keys. Confirm the selection is visible.
5. Press Ctrl+C. Confirm the clipboard contains the selected text.
6. Use Ctrl+A on a result longer than one page. Confirm the full result is selected.
7. Press Escape. Confirm focus returns to the previous application and scanning updates the popup again.
8. Repeat pinning and unpinning with the shortcut. Check focus after each action.
9. Repeat across multiple monitors. Check clipping and focus when moving between outputs.
10. Repeat with large text and high-contrast colors. Check that the result remains readable.
11. Repeat with Orca or another screen reader. Check announcements, reading order and selection feedback.
12. Dismiss the pinned result. Confirm the screen reader returns to the previous application.

<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Architecture

MaruPop is a native Qt Widgets application for Japanese text recognition and dictionary lookup
on Plasma Wayland. Hyprland has an experimental backend. Build requirements are in
[README.md](../README.md).

## Components

| Directory | Responsibility |
| --- | --- |
| `src/app/` | Service ownership, tray, global shortcuts, settings and lookup window |
| `src/core/` | KConfigXT settings, paths, enums and logging categories |
| `src/platform/` | Session detection and construction of capture, cursor, lock and shortcut services |
| `src/wayland/` | Protocol registry and shared-memory buffers on Qt's Wayland connection |
| `src/capture/` | Screen capture, authorization, coordinate conversion and scan-region geometry |
| `src/cursor/` | KWin cursor relay, Hyprland socket polling and lock watchers |
| `src/ocr/` | Recognition backends, preprocessing, corrections, grouping, hit testing and model downloads |
| `src/jp/` | Japanese classification, normalization and expression boundaries |
| `src/deconj/` | Rule-based deconjugation and process descriptions |
| `src/dict/` | Dictionary downloads, imports, immutable stores and published snapshots |
| `src/lookup/` | Candidate generation, queries, ranking, decoration and result caching |
| `src/scan/` | Capture/recognition/lookup state machine, throttling and cached scans |
| `src/popup/` | Rich-text rendering, pitch contours, placement and pinned results |
| `src/dictui/` | Dictionary management and import configuration |

`data/` contains desktop metadata, notifications, the KWin relay package and deconjugation rules.
`third_party/` contains component notices and protocol sources; [NOTICE](../NOTICE) records
provenance. [Development tools](../tools/README.md) reproduce individual subsystem behavior.
[Test documentation](TESTING.md) describes isolated and compositor-backed verification.

## Platform services

`platform::Backend` constructs four interfaces: `capture::FrameSource`,
`cursor::CursorTracker`, `cursor::LockWatcher` and `ShortcutRegistry`. Application code uses
those interfaces. `MARUPOP_PLATFORM=kde|hyprland|wlroots|unknown` overrides session detection.
Hyprland's socket takes precedence over a reachable KWin D-Bus name because a nested compositor
can inherit the parent Plasma bus.

On Plasma, `KWinGrabber` calls `org.kde.KWin.ScreenShot2.CaptureArea`. KWin authorizes the
caller by matching its executable to an installed desktop entry declaring the restricted
interface. Configure the intended installation prefix before building; stage with `DESTDIR`.
`tools/install-dev-desktop.sh` provides matching launchers for build-tree executables.

The KWin script calls MaruPop's `/Cursor` D-Bus object. `CursorSink::Update` accepts integral
and fractional scale values through separate overloads because JavaScript D-Bus marshaling
chooses the signature from the value. The returned tracking state lets the script reduce
polling while scanning is inactive. Register the sink before loading the script. A service
watcher restores the relay after KWin re-registers its bus name.

On Hyprland, `HyprCursorTracker` reads `cursorpos` from socket1 and `WlrFrameSource` uses
`zwlr_screencopy_v1.capture_output_region`. A pending capture permission can produce neither
`ready` nor `failed`, so each capture has a watchdog. Fixed capture tiles bound the set of
compositor capture sessions created as the pointer moves. The popup's `no_screen_share` layer
rule prevents self-recognition; the captured overlap is black, so `Frame::occluded` also
prevents hit testing there and popup placement avoids the recognized paragraph.

Protocol bindings use `wl::Registry` on the display owned by Qt. Generate the client bindings
in `src/CMakeLists.txt`, where the consuming `marupop_lib` target is defined. Compositor
configuration helpers emit both Lua and hyprlang forms, according to the detected configuration.

## Scan loop and coordinate spaces

The pointer enters `scan::ScanController`. Cached recognition supplies a hit immediately when
its region remains valid. A changed frame or movement outside the region schedules capture,
frame hashing and recognition on the OCR worker. Lookup then runs on the global thread pool.
Recognition results are cached by rectangle and pixel hash.

If the hit paragraph reaches the frame edge, the initial answer is emitted before a larger
region is requested. Growth is limited by the workspace, source quantization and detector
resolution. A fixed detector input makes characters smaller when the source region grows;
`ocr::minimumCharExtent()` bounds that tradeoff. Compare the quantized target with the current
rectangle before growing, so a small output cannot cause repeated captures of the same tile.
A fallback to a smaller region must preserve the resolution gate's conditions to avoid
alternating indefinitely between two region sizes.

`lookupReady()` carries new content and a `HitContext`. `hitMoved()` carries position updates
for the current hit, allowing the popup to follow the pointer without rerendering every sample.
Emit position updates after hit testing so leaving the recognized text dismisses the result.
A requested rectangle, scan generation and lookup ticket reject answers from superseded work.
An alive flag protects queued completion after controller destruction.

Three coordinate spaces meet here:

- Logical desktop coordinates describe the pointer, requested capture and layer-shell margins.
- Source-image pixels describe `CharBox`, `TextLine` and `Paragraph` rectangles.
- Device pixels account for each output's device-pixel ratio.

`capture::imageToLogical()` and `logicalToImage()` own conversion between image and desktop
coordinates. Screen layout code accounts for mixed output scales. Tests use a fixed synthetic
page where the text stays at desktop coordinates as the capture rectangle changes.

## Concurrency and dictionary lifetime

Recognition, dictionary import and lookup run off the GUI thread. Asynchronous handoffs use a
generation counter, guarded owner pointer and queued completion on the owner's thread.
`ocr::OcrService` has one pending request slot; a newer request replaces queued work.

`DictionaryManager` publishes an immutable `DictionarySnapshot` of handles owning open stores.
`lookup::Engine` takes a snapshot for each request. A result retains its dictionary handle, so
removing a dictionary cannot invalidate a result already being rendered. Snapshot identity is
part of the result-cache key. Store metadata and key-filter mappings are immutable after open;
a mutex protects the SQLite connection and decoded-record cache.

The Chrome Screen AI library is initialized once per process and called from one worker.
ONNX Runtime providers are accepted only after session construction and a validation inference,
because dynamic graphs can defer compilation until the first inference. Failed providers fall
back to CPU. MIGraphX disables CPU partition fallback to reject unsupported graph splits during
session construction. Provider availability and performance depend on the installed runtime;
use the optional backend tests and probes with the intended models and hardware.

## Text recognition and lookup

Every backend maintains `text.size() == chars.size()` for lines and paragraphs. Hit testing
uses the same index for the UTF-16 text and its character boxes. Screen AI text is rebuilt from
symbol boxes to maintain that contract. Corrections must preserve aligned text and geometry;
empty corrected lines are dropped. The correction rule data and standalone reference used by
synthetic tests are maintained through the [generator workflow](../tools/README.md).

`jp::normalizeText()` supplies shared normalization for imported keys and lookup candidates.
The deconjugator explores rule paths and retains tied process descriptions. Japanese line
classification uses kana and kanji ranges rather than treating fullwidth Latin as Japanese.
Chōonpu expansion is bounded to avoid uncontrolled candidate growth.

Exact and deconjugated queries run before optional character variants. Variant lookup replaces
one eligible character with a similar candidate and applies confidence, dictionary and result
filters. Its settings trade recall against false positives; synthetic fixtures establish rule
behavior. Ranking prioritizes matched span length and then the remaining ranking criteria.
JMdict part-of-speech data prevents a lemma from matching through an incompatible conjugation
class. Frequency and pitch data decorate results after the lexical query.

Each imported dictionary uses SQLite and key-filter sidecars. CBOR payloads carry type-specific
records. An incompatible codec produces `NeedsReimport`, so record format changes cause an
explicit reimport. A sorted XXH3 key filter rejects misses before SQLite queries; a hash
collision merely causes an extra query. Custom entries are appended to TSV sources and reimported.
Yomitan metadata banks dispatch by row mode so a source can provide frequency and pitch data.

## Popup and application integration

`PopupView` is a `QTextBrowser` shared by the result surface and settings preview. Transient
results follow the pointer without taking keyboard focus. Pinned results retain their content
and provide text interaction. The keyboard behavior and manual checks are documented in
[README.md](../README.md) and [TESTING.md](TESTING.md).

A layer surface is bound to one output at creation. Moving across outputs requires rebuilding
the surface and measuring margins from that output's origin; changing `QWindow::screen()`
alone cannot rebind it. Rebuilding must preserve the model and pinned state. Clear
`WA_TransparentForMouseEvents` before changing window flags when entering pinned mode, since
Qt can otherwise restore pointer transparency during creation.

The fade paints both the card and view with opacity because Wayland window opacity alone is
insufficient. Apply scrollbar styles to the scrollbar object to keep the viewport transparent.
Pitch anchors encode reading length because rich text attaches an anchor name only to the
first character. Force document layout before measuring those anchors. Font sizes use points
so logical DPI affects rendering.

Settings use the `kcfg_<Name>` binding convention. Shortcuts implement their own apply/reset
logic because they belong to compositor actions. Keep stored setting names stable when UI
labels change. Theme previews update controls before Apply instead of writing settings early.

`MARUPOP_APPLICATION_ID` is `io.github.marunine.marupop` across desktop metadata, icons and
D-Bus identity. The separate component name `marupop` owns the existing settings namespace.
KGlobalAccel's component name must omit `.desktop`, which otherwise selects desktop-action
launching. Set the organization domain before constructing `KDBusService` so its unique name
matches the cursor relay. Autostart respects `Hidden=true` set by the desktop and copies the
installed launcher to preserve capture authorization.

Register logging categories in the declaration, definition, `logCategoryNames()` and
`src/CMakeLists.txt`; `logging_test` checks consistency. Resource compilation uses an OBJECT
library exposed to executable consumers so the linker retains resource initialization.

## Maintenance

[CONTRIBUTING.md](../CONTRIBUTING.md) covers build, formatting, translations and reports.
[STYLE.md](STYLE.md) defines UI terminology and documentation conventions. Keep durable
contracts beside code and name the regression tests that protect them. Public source revisions
belong in provenance notices; performance claims require runnable inputs and commands.

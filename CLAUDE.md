<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Development guidance

Read [CONTRIBUTING.md](CONTRIBUTING.md) for build and review commands,
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for design contracts and
[docs/TESTING.md](docs/TESTING.md) for resource gates and compositor isolation.
Follow [docs/STYLE.md](docs/STYLE.md) for UI text and comments.

Use the `build/` directory for the compilation database. Configure Clang consistently for C and
C++ when using clangd or clang-tidy. Application, probe and test binaries are in `build/bin/`.
Use `.clang-format` and retain existing SPDX notices. Generated files must be updated with their
listed generators; preserve public source provenance and license notices.

KDE compiler settings disable implicit ASCII conversions and Qt keyword macros. Use
`QStringLiteral` or `u"…"_s`, and `Q_EMIT`, `Q_SIGNALS`, `Q_SLOTS`. Most application code is
built without exceptions; the ONNX Runtime translation units opt in where required.

Keep platform-specific service construction in `src/platform/`. Preserve snapshot ownership
across dictionary lookup, generation checks across asynchronous work and aligned OCR text/boxes.
A new logging category must be declared, defined, listed in `logCategoryNames()` and registered
with `marupop_logging_category()` in `src/CMakeLists.txt`.

Use existing test doubles. Fake well-known D-Bus services may register only behind the private
bus guard. Tests requiring models, network access or user-supplied dictionaries must skip with
an actionable reason when inputs are absent. Use a private compositor parent in automation.

Update `po/marupop.pot` after changing user-visible strings. Preserve stored setting keys,
shortcut identifiers and desktop application identity when changing labels. Document technical
conditions and regression tests without private workspace paths or unpublished history references.

<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Contributing

Submit changes through [GitHub pull requests](https://github.com/marunine/marupop/pulls).

## Build and verify

Follow the [README build and install instructions](README.md#building-and-installing). For
build-folder runs on Plasma, follow the [development launcher
instructions](tools/README.md#the-kwin-privilege-gate).

Add `-DCMAKE_BUILD_TYPE=Debug` and `-DMARUPOP_BUILD_DEV_TOOLS=ON` when configuring for development.

On Windows, use the `windows-msvc` or `windows-clang-cl` preset from the
[README](README.md#windows).

Verification steps:

1. Run the [tests](docs/TESTING.md#run-tests), or the [Windows tests](docs/TESTING.md#windows-tests).
2. Run `po/extract-messages.sh --check` to check the translation template.
3. Run the [compositor tests](docs/TESTING.md#compositor-tests) after changing capture,
   popup behavior, cursor tracking or shortcuts.
4. Run the [interactive desktop tests](docs/TESTING.md#interactive-desktop-tests) after changing
   the Windows capture, popup or pointer code.

See [TESTING.md](docs/TESTING.md) for test inputs and skip conditions.
The [CI workflow](.github/workflows/ci.yml) lists the automated checks and their dependencies.

Before submitting a pull request:

- Format changed C++ files with `clang-format -i path/to/file.cpp`.
- Follow [STYLE.md](docs/STYLE.md) for UI text and documentation.
- Keep each commit focused. Use a short imperative commit title.
- Describe the changed behavior and test results in the pull request.
- Explain skipped checks and platform limitations.
- Include a regression test when the suite can reproduce the bug.

To enable the supplied pre-commit hook, configure with `-DMARUPOP_GIT_HOOKS=ON`. The hook runs
three CI checks on the staged changes:

- clang-format over the staged C++ files
- `po/extract-messages.sh --check` over the staged translation template
- `tools/clang-tidy-diff.sh` over the staged lines of `src/`, `tests/` and `tools/`

The clang-tidy check reads `build/compile_commands.json`. `MARUPOP_BUILD_DIR` names another build
folder, and `MARUPOP_SKIP_CLANG_TIDY=1` skips the check. The hook, the CI checks and the
`clang-format` target require the LLVM major version in
[`.clang-tools-version`](.clang-tools-version). `MARUPOP_CLANG_FORMAT` and `MARUPOP_CLANG_TIDY`
name binaries of that version for the hook, and `CLANG_FORMAT_EXECUTABLE` names one for the
target.

## Packaging

1. Configure with the final `CMAKE_INSTALL_PREFIX`, `BUILD_TESTING=OFF` and `MARUPOP_BUILD_DEV_TOOLS=OFF`.
   The desktop entry embeds the executable path during configuration.
2. Stage the installation with `DESTDIR=/path/to/stage cmake --install build`.
   Using `cmake --install --prefix` leaves the embedded path incorrect.
3. Check the staged desktop entry with `desktop-file-validate`.
4. Check the AppStream metadata with `appstreamcli validate --no-net --pedantic`.
5. Verify that the desktop entry's `Exec` uses the final executable path.
6. Check the installed notices against [NOTICE](NOTICE).

### Windows packaging

1. Configure with `cmake --preset windows-msvc`.
   The build folder is `build`.
2. Build with `cmake --build --preset windows-msvc`.
   The executables are in `build\bin`.
3. Install with `cmake --install build --prefix <folder>`.
   The installed executable is `<folder>\bin\marupop.exe`.
4. Run `<folder>\bin\marupop.exe --check-authorization`.
   The report ends with `Screen capture: available.`
5. Check the notices under `<folder>\share\doc\marupop` against [NOTICE](NOTICE).

Update the `REF`, `SHA512` and version of the ports under `packaging/vcpkg/ports/` when the
`vcpkg.json` baseline changes the KDE Frameworks release.

## Generated files and attribution

- Use the [regeneration commands](tools/README.md#regenerating-data) when changing generated data.
- Update the source inputs and generated files together.
- Record a public source URL, revision and license for imported data.
- Keep local checkout paths out of generated files.
- Preserve copyright notices and SPDX identifiers.
- Document the source and license of new fixtures, images and copied code in
  [NOTICE](NOTICE) or the component's notice file.
- Use original or synthetic text in screenshots and tests.

## Translations

Submit translations by pull request:

1. Run `po/extract-messages.sh` to update the message template.
2. Create `po/<locale>/marupop.po` from `po/marupop.pot`, or merge the template into an existing catalogue.
3. Check the catalogue headers and plural forms for the locale.
4. Run `msgfmt --check -o /dev/null po/<locale>/marupop.po` to validate the catalogue.
5. Build and review the UI with the intended locale.

Follow the [UI terminology and label rules](docs/STYLE.md#user-interface).

## Bug and security reports

Report bugs through [GitHub Issues](https://github.com/marunine/marupop/issues). Include:

- Application version.
- Compositor.
- Steps to reproduce.
- Expected behavior.
- Dependency versions, when relevant.

Before sharing a report:

- Use synthetic Japanese text and a minimal dictionary fixture to reproduce the problem.
- Review diagnostics and screenshots for private text and local paths.

Crash dumps may contain screen content. A crash dump is optional for an initial report.

For security issues:

- Use GitHub's **Security → Report a vulnerability** option if available.
- Ask the maintainer for a private reporting channel if that option is unavailable.
- Keep exploit details and private data out of public issues.

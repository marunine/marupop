<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Contributing

Submit changes through [GitHub pull requests](https://github.com/marunine/marupop/pulls).

## Build and verify

Follow the [README build and install instructions](README.md#building-and-installing). For
build-folder runs on Plasma, follow the [development launcher
instructions](tools/README.md#the-kwin-privilege-gate).

Add `-DCMAKE_BUILD_TYPE=Debug` and `-DMARUPOP_BUILD_DEV_TOOLS=ON` when configuring for development.

Verification steps:

1. Run the [tests](docs/TESTING.md#run-tests).
2. Run `po/extract-messages.sh --check` to check the translation template.
3. Run the [compositor tests](docs/TESTING.md#compositor-tests) after changing capture,
   popup behavior, cursor tracking or shortcuts.

See [TESTING.md](docs/TESTING.md) for test inputs and skip conditions.
The [CI workflow](.github/workflows/ci.yml) lists the automated checks and their dependencies.

Before submitting a pull request:

- Format changed C++ files with `clang-format -i path/to/file.cpp`.
- Follow [STYLE.md](docs/STYLE.md) for UI text and documentation.
- Keep each commit focused. Use a short imperative commit title.
- Describe the changed behavior and test results in the pull request.
- Explain skipped checks and platform limitations.
- Include a regression test when the suite can reproduce the bug.

To enable the supplied pre-commit hook, configure with `-DMARUPOP_GIT_HOOKS=ON`. It checks the
staged C++ formatting and the staged translation template, as the CI format job does.

## Packaging

1. Configure with the final `CMAKE_INSTALL_PREFIX`, `BUILD_TESTING=OFF` and `MARUPOP_BUILD_DEV_TOOLS=OFF`.
   The desktop entry embeds the executable path during configuration.
2. Stage the installation with `DESTDIR=/path/to/stage cmake --install build`.
   Using `cmake --install --prefix` leaves the embedded path incorrect.
3. Check the staged desktop entry with `desktop-file-validate`.
4. Check the AppStream metadata with `appstreamcli validate --no-net --pedantic`.
5. Verify that the desktop entry's `Exec` uses the final executable path.
6. Check the installed notices against [NOTICE](NOTICE).

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

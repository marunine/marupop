#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Installs a desktop entry pointing at a build tree so KWin's privilege gate
# (X-KDE-DBUS-Restricted-Interfaces / X-KDE-Wayland-Interfaces, matched via /proc/pid/exe
# against the entry's absolute Exec) authorizes the build-tree binary. Development only; the
# installed application uses the entry generated from data/<app-id>.desktop.cmake instead.
#
# Usage: tools/install-dev-desktop.sh <build-dir> | --uninstall
set -euo pipefail

apps_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
# Must match MARUPOP_APPLICATION_ID in CMakeLists.txt: KWin resolves an entry by name and
# KGlobalAccel keys its shortcuts on it, so an entry under any other name authorizes nothing.
app_id=io.github.marunine.marupop
# The probes under tools/ that bind a restricted Wayland global or call a restricted D-Bus
# interface. Each name is a target this script writes an entry for, as
# marupop-<name>.desktop naming <build-dir>/bin/marupop-<name>.
probes=(captureprobe)
# The probes that bind org_kde_kwin_fake_input. KWin advertises that global only to a client
# whose entry names it, and it is not one of the two interfaces the other entries declare, so
# these get an entry of their own.
fake_input_probes=(hoverprobe)
# The test binaries that call a restricted interface. KWin matches /proc/pid/exe against an
# installed Exec, so a test calling org.kde.KWin.ScreenShot2 needs an entry of its own or it
# self-skips on NoAuthorized. Each name is a binary at <build-dir>/bin/<name>, written as
# marupop-test-<name>.desktop.
test_binaries=(framesource_test)

if [[ "${1:-}" == "--uninstall" ]]; then
    for probe in ${probes[@]+"${probes[@]}"} ${fake_input_probes[@]+"${fake_input_probes[@]}"}; do
        rm -f "$apps_dir/marupop-$probe.desktop"
    done
    for binary in ${test_binaries[@]+"${test_binaries[@]}"}; do
        rm -f "$apps_dir/marupop-test-$binary.desktop"
    done
    rm -f "$apps_dir/$app_id.desktop"
    kbuildsycoca6 >/dev/null 2>&1 || true
    echo "removed dev desktop entries"
    exit 0
fi

build_dir="$(realpath "${1:?usage: $0 <build-dir> | --uninstall}")"
mkdir -p "$apps_dir"

# KDECMakeSettings collects every executable in <build-dir>/bin, so the paths below hold for
# any build configured through the project's CMakeLists.
for probe in ${probes[@]+"${probes[@]}"}; do
    exe="$build_dir/bin/marupop-$probe"
    cat > "$apps_dir/marupop-$probe.desktop" <<EOF
[Desktop Entry]
Name=MaruPop probe: $probe
Exec=$exe
Type=Application
Terminal=true
NoDisplay=true
X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
EOF
done

for probe in ${fake_input_probes[@]+"${fake_input_probes[@]}"}; do
    cat > "$apps_dir/marupop-$probe.desktop" <<EOF
[Desktop Entry]
Name=MaruPop probe: $probe
Exec=$build_dir/bin/marupop-$probe
Type=Application
Terminal=true
NoDisplay=true
X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input
EOF
done

for binary in ${test_binaries[@]+"${test_binaries[@]}"}; do
    cat > "$apps_dir/marupop-test-$binary.desktop" <<EOF
[Desktop Entry]
Name=MaruPop test: $binary
Exec=$build_dir/bin/$binary
Type=Application
Terminal=true
NoDisplay=true
X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
EOF
done

# The application itself, so a build-tree binary is authorized without installing it. The file
# name has to be the application id: KGlobalAccel keys its shortcuts on it and KWin resolves
# the entry through it. It therefore shadows an installed entry — remove it with --uninstall
# before running an installed build.
cat > "$apps_dir/$app_id.desktop" <<EOF
[Desktop Entry]
Name=MaruPop (build tree)
Exec=$build_dir/bin/marupop
Type=Application
Terminal=false
NoDisplay=true
X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
EOF

kbuildsycoca6 >/dev/null 2>&1 || true
echo "installed a dev desktop entry for the marupop binary"
echo "Exec prefix: $build_dir/bin/"

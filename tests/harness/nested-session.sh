#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Runs a command inside a nested KWin Wayland session that owns a private D-Bus session bus, a
# private XDG_RUNTIME_DIR and a private HOME. The nested session provides the four services the
# live-gated suites need: org.kde.KWin.ScreenShot2 at /org/kde/KWin/ScreenShot2,
# org.kde.kwin.Scripting at /Scripting, the zwlr_layer_shell_v1 and org_kde_kwin_fake_input
# Wayland globals, and QScreen geometry from a virtual output. A suite therefore runs the
# production D-Bus and Wayland code paths on a machine with no logged-in Plasma session, and
# without reading or writing anything in the session of the user who starts it.
#
# Usage: nested-session.sh --home DIR [options] -- COMMAND [ARGS...]
#
#   --home DIR         Session HOME, created if absent. Holds the desktop entries and the kwinrc
#                      the nested compositor writes. Required.
#   --authorize PATH   Writes a desktop entry naming PATH with
#                      X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2. Repeatable.
#   --fake-input PATH  Writes a desktop entry naming PATH with
#                      X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input. Repeatable.
#   --width N          Virtual output width in physical pixels. Default 1920.
#   --height N         Virtual output height in physical pixels. Default 1080.
#   --scale N          Virtual output scale. Default 1.
#   --outputs N        Virtual output count. Default 1.
#   --lockscreen       Starts the session with lock screen support, which registers
#                      org.freedesktop.ScreenSaver. The default passes --no-lockscreen.
#
# Exit code: the command's own exit code, which kwin_wayland propagates through
# --exit-with-session. Exit code 77 reports that the host lacks kwin_wayland or
# dbus-run-session, which is the value tests/CMakeLists.txt sets as SKIP_RETURN_CODE.
set -euo pipefail

readonly SKIP_EXIT=77

# The largest XDG_RUNTIME_DIR parent this script may use, in bytes. The AF_UNIX sun_path limit
# is 108 bytes including the terminator, leaving 107. The path kwin_wayland binds is the parent,
# plus the 15 bytes mktemp -d appends for "/mp-nest.XXXXXX", plus up to 11 for "/wayland-10",
# which is 26 bytes this script adds to whatever the parent is. A longer parent makes
# kwin_wayland abort with "plus null terminator exceeds 108 bytes" before it maps an output.
# CTest binary directories under a deep source tree exceed it, which is why the runtime
# directory is never derived from the build directory.
readonly RUNTIME_PATH_BUDGET=81

home=""
width=1920
height=1080
scale=1
outputs=1
lockscreen_flag="--no-lockscreen"
authorize=()
fake_input=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --home) home="$2"; shift 2 ;;
        --authorize) authorize+=("$2"); shift 2 ;;
        --fake-input) fake_input+=("$2"); shift 2 ;;
        --width) width="$2"; shift 2 ;;
        --height) height="$2"; shift 2 ;;
        --scale) scale="$2"; shift 2 ;;
        --outputs) outputs="$2"; shift 2 ;;
        --lockscreen) lockscreen_flag="--lockscreen"; shift ;;
        --) shift; break ;;
        *) echo "nested-session.sh: unknown option $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$home" ]]; then
    echo "nested-session.sh: --home is required" >&2
    exit 2
fi
if [[ $# -eq 0 ]]; then
    echo "nested-session.sh: a command is required after --" >&2
    exit 2
fi

for required in kwin_wayland dbus-run-session; do
    if ! command -v "$required" >/dev/null 2>&1; then
        echo "nested-session.sh: $required is absent; skipping" >&2
        exit "$SKIP_EXIT"
    fi
done

mkdir -p "$home"
home="$(realpath "$home")"
apps_dir="$home/.local/share/applications"
mkdir -p "$apps_dir"

# KWin matches /proc/<pid>/exe of the caller against the first token of Exec in an installed
# desktop entry, then reads the two X-KDE-* keys from the matched entry. Both keys hold a
# comma-separated list, which is the form
# xdg-desktop-portal-kde/data/org.freedesktop.impl.portal.desktop.kde.desktop.in uses for the same
# two globals this script writes.
#
# One entry per executable rather than one per role. KWin reads the keys of the single entry it
# matched, so a binary named by both --authorize and --fake-input needs one entry carrying both
# globals: two entries at one path had the second write drop the first one's keys, and a suite
# registered with both options was refused org.kde.KWin.ScreenShot2.
declare -A dbus_for=()
declare -A wayland_for=()
declare -a entry_exes=()

# Adds one role to an executable, creating its entry the first time it is named. The explicit
# `return 0` is required: the last command is a conditional append, and a false one would make the
# function's status 1 and `set -e` end the script with no diagnostic.
add_role() {
    local exe="$1" dbus_interface="$2" wayland_interface="$3"
    if [[ -z "${dbus_for[$exe]+set}" && -z "${wayland_for[$exe]+set}" ]]; then
        entry_exes+=("$exe")
        dbus_for["$exe"]=""
        wayland_for["$exe"]=""
    fi
    if [[ -n "$dbus_interface" ]]; then
        dbus_for["$exe"]="${dbus_for[$exe]:+${dbus_for[$exe]},}$dbus_interface"
    fi
    if [[ -n "$wayland_interface" ]]; then
        wayland_for["$exe"]="${wayland_for[$exe]:+${wayland_for[$exe]},}$wayland_interface"
    fi
    return 0
}

write_entry() {
    local exe="$1" dbus_interfaces="$2" wayland_interfaces="$3"
    local name
    name="$(basename "$exe")"
    {
        echo "[Desktop Entry]"
        echo "Name=MaruPop nested session: $name"
        echo "Exec=$exe"
        echo "Type=Application"
        echo "Terminal=true"
        echo "NoDisplay=true"
        if [[ -n "$dbus_interfaces" ]]; then
            echo "X-KDE-DBUS-Restricted-Interfaces=$dbus_interfaces"
        fi
        if [[ -n "$wayland_interfaces" ]]; then
            echo "X-KDE-Wayland-Interfaces=$wayland_interfaces"
        fi
    } > "$apps_dir/marupop-nested-$name.desktop"
}

for exe in ${authorize[@]+"${authorize[@]}"}; do
    add_role "$exe" "org.kde.KWin.ScreenShot2" ""
done
for exe in ${fake_input[@]+"${fake_input[@]}"}; do
    add_role "$exe" "" "org_kde_kwin_fake_input"
done
for exe in ${entry_exes[@]+"${entry_exes[@]}"}; do
    write_entry "$exe" "${dbus_for[$exe]}" "${wayland_for[$exe]}"
done

# A directory under the caller's XDG_RUNTIME_DIR where that path fits the budget above, and
# under /tmp otherwise. Both are on a filesystem that supports AF_UNIX sockets, which /dev/shm
# on some hosts does not.
runtime_parent="${XDG_RUNTIME_DIR:-/tmp}"
if [[ ${#runtime_parent} -gt $RUNTIME_PATH_BUDGET || ! -d "$runtime_parent" ]]; then
    runtime_parent=/tmp
fi
runtime_dir="$(mktemp -d "$runtime_parent/mp-nest.XXXXXX")"
chmod 700 "$runtime_dir"

# Cleanup must preserve the test exit status and tolerate delayed portal shutdown. A live
# xdg-document-portal FUSE mount can prevent removal, and a private-bus service can recreate
# its runtime files after the first pass. Retry after shutdown without letting a failed removal
# abort the EXIT trap under set -e.
cleanup() {
    local status=$?
    # The first two passes are unconditional and the pause between them is what catches the
    # recreated dconf: testing for absence and returning early misses that case, because the
    # directory is gone at that moment and recreated afterwards.
    rm -rf "$runtime_dir" 2>/dev/null || true
    sleep 0.5
    rm -rf "$runtime_dir" 2>/dev/null || true
    # A third pass runs only where the directory survived both, which is the FUSE mount rather than
    # the recreation, and needs longer than the pause above.
    if [[ -e "$runtime_dir" ]]; then
        sleep 1
        rm -rf "$runtime_dir" 2>/dev/null || true
    fi
    return "$status"
}
trap cleanup EXIT

# The command runs as the session application. QT_QPA_PLATFORM=wayland puts it on the nested
# compositor, which is what makes PopupWindow take its zwlr_layer_shell_v1 branch rather than
# the offscreen fallback. kbuildsycoca6 rebuilds the service cache in the session HOME so the
# entries written above are visible to the KWin authorization lookup.
#
# The status file carries the command's own exit code out of the session. Two things make the
# exit code of the whole pipeline unusable: kwin_wayland exits before it maps an output on a
# host with no working EGL, which is a skip rather than a failure, and xdg-desktop-portal
# autostarts on the private bus and fails during teardown after the command has already
# succeeded, which turned a passing run into exit 1. An absent status file means
# the command never ran.
status_file="$runtime_dir/session-status"
started_marker="$runtime_dir/session-started"
session_script="$runtime_dir/session.sh"
{
    echo "#!/usr/bin/env bash"
    echo "touch ${started_marker@Q}"
    echo "export QT_QPA_PLATFORM=wayland"
    echo "export MARUPOP_NESTED_SESSION=1"
    echo "kbuildsycoca6 >/dev/null 2>&1 || true"
    printf '%s' "$(printf '%q ' "$@")"
    echo
    echo "status=\$?"
    echo "printf '%s' \"\$status\" > ${status_file@Q}"
    echo "exit \$status"
} > "$session_script"
chmod +x "$session_script"

# LANG is forced to a UTF-8 locale because Qt switches to C.UTF-8 with a warning on stderr
# otherwise, and the suites compare Japanese strings.
set +e
env \
    HOME="$home" \
    XDG_RUNTIME_DIR="$runtime_dir" \
    XDG_DATA_HOME="$home/.local/share" \
    XDG_CONFIG_HOME="$home/.config" \
    XDG_CACHE_HOME="$home/.cache" \
    XDG_STATE_HOME="$home/.local/state" \
    LANG="${LANG:-C.UTF-8}" \
    QT_LOGGING_RULES="${QT_LOGGING_RULES:-}" \
    QT_FORCE_STDERR_LOGGING=1 \
    dbus-run-session -- \
    kwin_wayland \
        --virtual \
        --width "$width" \
        --height "$height" \
        --scale "$scale" \
        --output-count "$outputs" \
        "$lockscreen_flag" \
        --exit-with-session "$session_script"
compositor_status=$?
set -e

# A status file the session script never wrote has two causes, and only one of them is a skip.
# The compositor that never mapped an output writes nothing to its own marker either, and that
# is an environment without working EGL. A compositor that started and then failed to run the
# command is a defect, and reporting it as a skip would hide it: SKIP_RETURN_CODE turns 77 into
# a CTest pass.
if [[ ! -s "$status_file" ]]; then
    if [[ -e "$started_marker" ]]; then
        echo "nested-session.sh: the compositor started but the command left no exit status;" \
             "kwin_wayland exited with $compositor_status" >&2
        exit "$((compositor_status == 0 ? 1 : compositor_status))"
    fi
    echo "nested-session.sh: kwin_wayland exited with $compositor_status before it started a session;" \
         "the host cannot run a nested compositor, so this test is skipped" >&2
    exit "$SKIP_EXIT"
fi
exit "$(cat "$status_file")"

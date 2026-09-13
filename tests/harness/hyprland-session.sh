#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Runs a command inside a nested Hyprland session that owns a private XDG_RUNTIME_DIR, a private
# HOME and a generated configuration. The nested session provides the four things the
# wlroots-family suites need: the zwlr_screencopy_manager_v1, hyprland_lock_notifier_v1,
# hyprland_global_shortcuts_manager_v1 and zwlr_layer_shell_v1 Wayland globals, socket1 with its
# `cursorpos` command, QScreen geometry from one or more outputs, and the `no_screen_share` layer
# rule and `global` key bindings the suites assert on.
#
# **The configuration it generates is Lua.** Hyprland reads either a Lua configuration or a
# hyprlang one and chooses by the file's extension (0.56.2, src/config/ConfigManager.cpp:33 and
# :45), and the hyprlang manager is already deleted from upstream main, so a .conf harness would
# stop working in the release after 0.56.2. Lua landed in 0.55.0; a host below that cannot run
# this harness at all. Three things follow from the choice, and each is stated where it bites:
# `hyprctl dispatch` takes a Lua expression rather than a keyword, Hyprland exports no
# WAYLAND_DISPLAY to the child hl.exec_cmd() starts, and the top-level hl.exec_cmd() is what
# `exec-once` was.
#
# It is the counterpart of nested-session.sh, which does the same for kwin_wayland, and it keeps
# that script's three conventions: a private runtime directory whose path fits the AF_UNIX
# sun_path budget, an exit-status file carried out of the session, and exit code 77 for a host
# that cannot run the compositor at all.
#
# **Hyprland needs a parent Wayland compositor, and this script starts one of its own.** Hyprland
# asks Aquamarine for three backend implementations: headless as mandatory, DRM if available,
# Wayland as a fallback (Hyprland 0.56.0, src/Compositor.cpp:313 to :323). The headless backend
# carries no GPU and so supplies no allocator of its own; the allocator comes from DRM or from
# Wayland, and a start with neither ends in "Cannot open backend: no allocator available".
#
# DRM is unavailable to a second compositor on a seat another session already holds. It fails
# before any device is considered, in libseat rather than in device selection: the logind backend
# answers "Could not take control of session: Device or resource busy" and the seatd backend finds
# no /run/seatd.sock, so Aquamarine reports "libseat: failed to open a seat" and "DRM Backend
# failed" on Hyprland 0.56.2 and aquamarine 0.15.0-2 under Plasma.
#
# That leaves the Wayland backend, which needs a parent. By default the parent is a private
# kwin_wayland --virtual that nested-session.sh starts, because kwin_wayland renders headless and
# takes no seat: the run then touches nothing the caller can see, survives a locked screen, and
# does not compete with the caller's compositor for frame callbacks. `--parent caller` uses the
# caller's own compositor instead, which is the only form available where kwin_wayland is absent,
# and which does open a window on the caller's screen.
#
# The Hyprland wiki documents AQ_NO_KMS_REQUIREMENT, which lets the DRM backend accept a
# render-only node and would remove the parent entirely. It is not in aquamarine 0.15.0-2: that
# release reads exactly AQ_DRM_DEVICES, AQ_FORCE_LINEAR_BLIT, AQ_LIBINPUT_NO_PLUGINS,
# AQ_MGPU_NO_EXPLICIT, AQ_NO_ATOMIC, AQ_NO_MODIFIERS and AQ_TRACE, and setting it changes
# nothing: startup fails in libseat before device selection. Revisit this script when aquamarine gains the variable.
#
# Usage: hyprland-session.sh --home DIR [options] -- COMMAND [ARGS...]
#
#   --home DIR         Session HOME, created if absent. Holds the generated hyprland.lua.
#                      Required.
#   --width N          Output width in logical pixels. Default 1920.
#   --height N         Output height in logical pixels. Default 1080.
#   --scale N          Output scale. Default 1.
#   --outputs N        Output count, laid out left to right. Default 1. The first is the nested
#                      WAYLAND-1 window; the rest are headless outputs the session script adds
#                      with `hyprctl output create headless`.
#   --permission       Adds hl.permission() rules for screencopy and cursorpos and turns
#                      ecosystem.enforce_permissions on, which is what covers the gated branch.
#                      The default leaves enforcement off, which is Hyprland's own default.
#   --enforce-only     Turns ecosystem.enforce_permissions on and writes no rule, so every
#                      screencopy permission stays pending and a copy is answered with neither
#                      ready nor failed. Exports MARUPOP_HYPRLAND_PENDING_PERMISSION=1, which is
#                      what the suite gates its watchdog case on.
#   --parent MODE      Where Hyprland's allocator comes from. `auto` (the default) starts a
#                      private kwin_wayland --virtual, and falls back to the caller's compositor
#                      both where kwin_wayland or dbus-run-session is absent and where the private
#                      parent is present but does not start, so it never turns a suite that would
#                      have run into a skip. `kwin` requires the private parent and skips with 77
#                      instead of falling back. `caller` uses the caller's own compositor, which
#                      opens a window on their screen and cannot run while their session is
#                      locked.
#
# Exit code: the command's own exit code. Exit code 77 reports that the host lacks Hyprland, that
# `--parent kwin` found no usable kwin_wayland, or that the caller's compositor is the parent and
# is missing or locked, which is the value
# tests/CMakeLists.txt sets as SKIP_RETURN_CODE.
set -euo pipefail

readonly SKIP_EXIT=77

# The longest path Hyprland binds under the runtime directory is its socket2:
# "<runtime>/hypr/<signature>/.socket2.sock". "/hypr/" is 6 bytes, the instance signature is 62
# (40 hex digits of a SHA-1, an underscore, a 10-digit time, an underscore and a 10-digit random
# value), and "/.socket2.sock" is 14, which is 82 bytes of suffix. CUnixImpl refuses a path
# longer than sizeof(sun_path) - 1, which is 107 (Hyprland 0.56.0, src/ipc/s2/Unix.cpp:94), and
# answers "Socket2 path is too long. (2) IPC will not work." That leaves 25 bytes for the runtime
# directory itself. The mktemp template below adds 10, so the parent may be at most 15 bytes:
# /run/user/1000 is 14 and fits, and anything longer falls back to /tmp.
readonly RUNTIME_SUFFIX_BYTES=82
readonly SUN_PATH_BUDGET=107
readonly RUNTIME_TEMPLATE_BYTES=10

# Kept for the re-exec below, which runs this same script inside the parent it starts.
original_args=("$@")
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

home=""
width=1920
height=1080
scale=1
outputs=1
permission=0
enforce_only=0
parent_mode=auto
# Internal, set by the first pass on the command it runs inside the parent it started. Never
# passed by hand: it asserts that WAYLAND_DISPLAY already names a private compositor.
parent_private=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --parent-is-private) parent_private=1; shift ;;
        --home) home="$2"; shift 2 ;;
        --width) width="$2"; shift 2 ;;
        --height) height="$2"; shift 2 ;;
        --scale) scale="$2"; shift 2 ;;
        --outputs) outputs="$2"; shift 2 ;;
        --permission) permission=1; shift ;;
        --enforce-only) enforce_only=1; shift ;;
        --parent) parent_mode="$2"; shift 2 ;;
        --) shift; break ;;
        *) echo "hyprland-session.sh: unknown option $1" >&2; exit 2 ;;
    esac
done

case "$parent_mode" in
    auto|kwin|caller) ;;
    *) echo "hyprland-session.sh: --parent takes auto, kwin or caller, not $parent_mode" >&2; exit 2 ;;
esac

if [[ -z "$home" ]]; then
    echo "hyprland-session.sh: --home is required" >&2
    exit 2
fi
if [[ $# -eq 0 ]]; then
    echo "hyprland-session.sh: a command is required after --" >&2
    exit 2
fi

for required in Hyprland hyprctl; do
    if ! command -v "$required" >/dev/null 2>&1; then
        echo "hyprland-session.sh: $required is absent; skipping" >&2
        exit "$SKIP_EXIT"
    fi
done

# The private parent. nested-session.sh starts kwin_wayland --virtual and runs this same script
# inside it, where WAYLAND_DISPLAY names that compositor and the branch below takes it as the
# caller's. The second pass is told so by --parent-is-private, which the first pass puts ahead of
# the arguments it was given.
#
# An option rather than an exported variable, for two reasons. It cannot be inherited: a variable
# would also be picked up by a pass whose caller happened to have it set, and because the same
# fact decides whether the lock probe below runs, that pass would nest on a possibly locked
# session with the probe disabled. And it cannot be undone by the arguments that follow, because
# the re-entry test reads this flag rather than --parent, so a caller's own `--parent auto` later
# on the line changes nothing.
#
# The parent gets the output size this session was asked for, so Hyprland's WAYLAND-1 window has
# somewhere to sit. The extra outputs --outputs asks for are headless ones the session script adds
# with hyprctl, and they need no room in the parent.
if [[ $parent_private -eq 0 && "$parent_mode" != caller ]]; then
    if command -v kwin_wayland >/dev/null 2>&1 && command -v dbus-run-session >/dev/null 2>&1; then
        mkdir -p "$home/parent-kwin"
        set +e
        "$script_dir/nested-session.sh" \
            --home "$home/parent-kwin" --width "$width" --height "$height" \
            -- "${BASH_SOURCE[0]}" --parent-is-private "${original_args[@]}"
        parent_status=$?
        set -e
        # Anything but the skip is this run's own answer, and --parent kwin was asked for the
        # private parent specifically, so its skip is the answer too.
        if [[ $parent_status -ne $SKIP_EXIT || "$parent_mode" == kwin ]]; then
            exit "$parent_status"
        fi
        # kwin_wayland is installed and could not start a session: no working EGL for its virtual
        # backend is the usual reason. `auto` means a parent rather than that parent, so the run
        # falls through to the caller's compositor instead of reporting a skip for a suite that
        # would have run there. Not an exec above, which is what makes this reachable.
        echo "hyprland-session.sh: the private kwin_wayland parent did not start; falling back to" \
             "the caller's compositor" >&2
    elif [[ "$parent_mode" == kwin ]]; then
        echo "hyprland-session.sh: --parent kwin needs kwin_wayland and dbus-run-session, and one" \
             "of them is absent; skipping" >&2
        exit "$SKIP_EXIT"
    fi
fi

# The parent compositor's socket, resolved to an absolute path against the caller's
# XDG_RUNTIME_DIR before that variable is replaced. wl_display_connect() treats a name holding a
# slash as a path and every other name as relative to XDG_RUNTIME_DIR, so the absolute form is
# what lets the nested session keep a runtime directory of its own.
parent_socket=""
if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    if [[ "$WAYLAND_DISPLAY" == /* ]]; then
        parent_socket="$WAYLAND_DISPLAY"
    elif [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then
        parent_socket="$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"
    fi
fi
if [[ -z "$parent_socket" || ! -S "$parent_socket" ]]; then
    echo "hyprland-session.sh: no parent Wayland compositor (WAYLAND_DISPLAY names no socket);" \
         "Aquamarine has no allocator without one, so this test is skipped" >&2
    exit "$SKIP_EXIT"
fi

# A locked parent can withhold frame callbacks from the nested compositor, leaving
# screencopy requests unanswered. Probe org.freedesktop.ScreenSaver before starting
# an explicitly requested host-parent session. A missing service does not block the run.
if [[ $parent_private -eq 0 ]] && command -v busctl >/dev/null 2>&1; then
    lock_state="$(busctl --user call org.freedesktop.ScreenSaver /ScreenSaver \
        org.freedesktop.ScreenSaver GetActive 2>/dev/null || true)"
    if [[ "$lock_state" == *"b true"* ]]; then
        echo "hyprland-session.sh: the parent session is locked, so its compositor sends the nested" \
             "one no frame callbacks and no region copy can complete; this test is skipped" >&2
        exit "$SKIP_EXIT"
    fi
fi

mkdir -p "$home"
home="$(realpath "$home")"

runtime_parent="${XDG_RUNTIME_DIR:-/tmp}"
budget=$((SUN_PATH_BUDGET - RUNTIME_SUFFIX_BYTES - RUNTIME_TEMPLATE_BYTES))
if [[ ${#runtime_parent} -gt $budget || ! -d "$runtime_parent" ]]; then
    runtime_parent=/tmp
fi
# Ten bytes, counting the leading slash: "/mp.XXXXXX".
runtime_dir="$(mktemp -d "$runtime_parent/mp.XXXXXX")"
chmod 700 "$runtime_dir"

# The removal repeats for the reasons nested-session.sh records: an EXIT trap's status replaces
# the script's own, and a service the compositor activated outlives it and recreates directories
# under XDG_RUNTIME_DIR.
cleanup() {
    local status=$?
    rm -rf "$runtime_dir" 2>/dev/null || true
    sleep 0.5
    rm -rf "$runtime_dir" 2>/dev/null || true
    if [[ -e "$runtime_dir" ]]; then
        sleep 1
        rm -rf "$runtime_dir" 2>/dev/null || true
    fi
    return "$status"
}
trap cleanup EXIT

# Resolve the test executable to an absolute path for the permission rule.
# Hyprland matches /proc/<pid>/exe by exact path or RE2 full match. A relative path
# can leave permission pending, making screencopy return without ready or failed.
command_path="$(command -v -- "$1" 2>/dev/null || printf '%s' "$1")"
command_path="$(realpath -- "$command_path" 2>/dev/null || printf '%s' "$command_path")"

config_dir="$home/.config/hypr"
mkdir -p "$config_dir"
config="$config_dir/hyprland.lua"

# A Lua string literal holding an arbitrary path. Only a backslash and a double quote need
# escaping inside a "..." literal, and a path holds neither newline nor NUL.
lua_string() {
    local s=${1//\\/\\\\}
    printf '"%s"' "${s//\"/\\\"}"
}

status_file="$runtime_dir/session-status"
started_marker="$runtime_dir/session-started"
session_script="$runtime_dir/session.sh"
# The command's own stdout and stderr. Hyprland starts an exec-once child detached, so its output
# does not reach this script's stdout the way kwin_wayland's --exit-with-session command does.
# The file is written by the session script and printed below, which is what puts a failing
# assertion in `ctest --output-on-failure`.
output_file="$runtime_dir/session-output"

# The outputs, laid out left to right. The first is the Wayland backend's own window, which
# Aquamarine names WAYLAND-1; a rule naming it is required, because the catch-all `preferred`
# mode is rejected by that backend with "pending state rejected: invalid mode" and the output
# comes up at the hardcoded 1280x720 fallback instead. The rest are headless outputs the session
# script creates, which Aquamarine names HEADLESS-1 upward: the counter is per backend, so the
# first headless output is HEADLESS-1 even though it is the second output of the session.
{
    echo "-- Generated by tests/harness/hyprland-session.sh. Not for a user session."
    echo "hl.monitor({ output = \"WAYLAND-1\", mode = \"${width}x${height}@60\", position = \"0x0\", scale = $scale })"
    for ((i = 1; i < outputs; ++i)); do
        echo "hl.monitor({ output = \"HEADLESS-$i\", mode = \"${width}x${height}@60\"," \
             "position = \"$((i * width))x0\", scale = $scale })"
    done
    echo "hl.monitor({ output = \"\", mode = \"${width}x${height}@60\", position = \"auto\", scale = $scale })"
    echo
    echo "-- The layer rule that keeps MaruPop's own popup out of the pixels a scan reads."
    echo "-- src/popup/popupwindow.cpp gives the surface this namespace through"
    echo "-- LayerShellQt::Window::setScope()."
    echo "hl.layer_rule({ match = { namespace = \"marupop-popup\" }, no_screen_share = true })"
    echo
    echo "-- The three global shortcuts src/app/wlrshortcuts.cpp registers. The compositor owns"
    echo "-- the key sequence, so these lines are what a press comes from."
    echo "hl.bind(\"SUPER + ALT + J\", hl.dsp.global(\"io.github.marunine.marupop:toggle-scanning\"))"
    echo "hl.bind(\"SUPER + ALT + C\", hl.dsp.global(\"io.github.marunine.marupop:copy-word\"))"
    echo "hl.bind(\"SUPER + ALT + P\", hl.dsp.global(\"io.github.marunine.marupop:pin-popup\"))"
    echo
    echo "hl.config({"
    echo "    -- Xwayland has no client here, and a nested Hyprland whose Xwayland cannot reach a"
    echo "    -- display answers \"(EE) could not connect to wayland server\" and then takes the"
    echo "    -- compositor down with a SIGSEGV on Hyprland 0.56.2."
    echo "    xwayland = { enabled = false },"
    echo "    -- A still desktop. A suite asserting that two grabs of one region hash equal needs"
    echo "    -- the background to hold still, and Hyprland's default wallpaper is an animated"
    echo "    -- gradient: with it on screen, six grabs of one 320x240 region differed in all"
    echo "    -- 76800 pixels."
    echo "    animations = { enabled = false },"
    echo "    decoration = { blur = { enabled = false }, shadow = { enabled = false } },"
    echo "    misc = {"
    echo "        disable_hyprland_logo = true,"
    echo "        disable_splash_rendering = true,"
    echo "        force_default_wallpaper = 0,"
    echo "        background_color = \"0xff202020\","
    echo "    },"
    if [[ $permission -eq 1 || $enforce_only -eq 1 ]]; then
        echo "    -- ecosystem.enforce_permissions is false by default, so this is the branch a"
        echo "    -- default session never reaches."
        echo "    ecosystem = { enforce_permissions = true },"
    fi
    echo "})"
    if [[ $permission -eq 1 ]]; then
        echo
        echo "hl.permission({ binary = $(lua_string "$command_path"), type = \"screencopy\", mode = \"allow\" })"
        echo "hl.permission({ binary = $(lua_string "$command_path"), type = \"cursorpos\", mode = \"allow\" })"
    elif [[ $enforce_only -eq 1 ]]; then
        echo
        echo "-- Enforcement on and no rule: every screencopy permission stays pending, and a"
        echo "-- pending permission makes CScreenshareFrame::copy() return without sending ready"
        echo "-- or failed. There is no prompt to answer either, because this session runs no"
        echo "-- agent that could show one."
    fi
    echo
    # The config file is executed once, at load, so a top-level hl.exec_cmd() is what `exec-once`
    # was. A reload would run it a second time; nothing reloads this one.
    echo "hl.exec_cmd($(lua_string "$session_script"))"
} > "$config"

# The command runs as the session application. QT_QPA_PLATFORM=wayland puts it on the nested
# compositor, which is what makes PopupWindow take its zwlr_layer_shell_v1 branch and
# wl::Registry bind the three protocols rather than answering nullptr.
#
# WAYLAND_DISPLAY is reset to the nested compositor's own socket: the value the environment below
# passes is the parent's absolute path, which Aquamarine's Wayland backend needs and the test
# binary must not inherit. Hyprland exports its own into the session's environment, and the
# unset-and-default here is what covers a Hyprland that did not.
#
# `hyprctl dispatch exit` ends the compositor once the command has answered, which is what
# --exit-with-session does for kwin_wayland.
{
    echo "#!/usr/bin/env bash"
    echo "touch ${started_marker@Q}"
    echo "export QT_QPA_PLATFORM=wayland"
    echo "export MARUPOP_NESTED_SESSION=1"
    echo "export MARUPOP_LIVE_HYPRLAND=1"
    if [[ $enforce_only -eq 1 ]]; then
        echo "export MARUPOP_HYPRLAND_PENDING_PERMISSION=1"
    fi
    # The compositor's own socket, asked of the compositor. Two values have to be replaced here:
    # the parent's absolute path, which this script passes to Hyprland and the command must not
    # inherit, and nothing at all -- Hyprland puts no WAYLAND_DISPLAY in the environment of the
    # child a Lua config's hl.exec_cmd() starts, and an unset value leaves the command's Qt
    # platform plugin answering "Failed to create wl_display (Connection refused)". `hyprctl
    # instances` needs no WAYLAND_DISPLAY of its own, and naming the socket it reports rather
    # than assuming wayland-1 keeps this correct wherever the runtime directory already holds
    # another compositor's socket.
    echo "if [[ -z \"\${WAYLAND_DISPLAY:-}\" || \"\${WAYLAND_DISPLAY}\" == /* ]]; then"
    echo "    export WAYLAND_DISPLAY=\"\$(hyprctl instances 2>/dev/null | awk '/wl socket:/ { print \$3; exit }')\""
    echo "    if [[ -z \"\$WAYLAND_DISPLAY\" ]]; then"
    # The guess is only correct where the runtime directory holds this compositor's socket alone,
    # which is why it is a diagnostic rather than a silent default: a changed hyprctl label would
    # otherwise land here on every run and be noticed only as a connection failure elsewhere.
    echo "        echo 'hyprland-session.sh: hyprctl instances named no wl socket; guessing wayland-1' >&2"
    echo "        export WAYLAND_DISPLAY=wayland-1"
    echo "    fi"
    echo "fi"
    # An exec-once child can start before the outputs exist. Wait for the requested
    # output count and configured modes so QScreen never sees the backend fallback size.
    echo "waitForOutputs() {"
    echo "    local wanted=\$1 i"
    echo "    for i in \$(seq 1 200); do"
    echo "        if [[ \$(hyprctl monitors 2>/dev/null | grep -cE '^\\s+${width}x${height}@') -ge \$wanted ]]; then"
    echo "            return 0"
    echo "        fi"
    echo "        sleep 0.05"
    echo "    done"
    echo "    echo 'hyprland-session.sh: only '\"\$(hyprctl monitors 2>/dev/null | grep -cE '^Monitor ')\"' outputs came up' >&2"
    echo "    return 1"
    echo "}"
    echo "waitForOutputs 1 || true"
    # The extra outputs. The Wayland backend supplies exactly one, so a registration asking for
    # more adds headless ones on top of it; the allocator the Wayland backend brought is what
    # makes a headless output work at all.
    for ((i = 1; i < outputs; ++i)); do
        echo "hyprctl output create headless >/dev/null 2>&1 || true"
    done
    if ((outputs > 1)); then
        echo "waitForOutputs $outputs || true"
    fi
    printf '%s' "$(printf '%q ' "$@")"
    printf ' > %s 2>&1' "${output_file@Q}"
    echo
    echo "status=\$?"
    echo "printf '%s' \"\$status\" > ${status_file@Q}"
    # Let Hyprland's share-stop timer clear the active capture flag before teardown.
    # Hyprland 0.56.2 can otherwise dereference its already destroyed screenshare manager
    # while deleting the session. The timer runs half a second after the last frame.
    echo "sleep 0.6"
    # A Lua config makes `hyprctl dispatch` take a Lua dispatcher expression rather than a
    # keyword: the bare `hyprctl dispatch exit` the hyprlang form used answers "hl.dispatch:
    # expected a dispatcher (e.g. hl.dsp.window.close())" and exit code 7, and the compositor
    # keeps running until the registration times out.
    echo "hyprctl dispatch 'hl.dsp.exit()' >/dev/null 2>&1 || true"
    echo "exit \$status"
} > "$session_script"
chmod +x "$session_script"

# LANG is forced to a UTF-8 locale because Qt switches to C.UTF-8 with a warning on stderr
# otherwise, and the suites compare Japanese strings.
#
# There is no dbus-run-session here. Hyprland needs no session bus, and the suites that run under
# this harness reach their compositor over Wayland and over socket1 rather than over D-Bus.
set +e
env \
    HOME="$home" \
    XDG_RUNTIME_DIR="$runtime_dir" \
    XDG_DATA_HOME="$home/.local/share" \
    XDG_CONFIG_HOME="$home/.config" \
    XDG_CACHE_HOME="$home/.cache" \
    XDG_STATE_HOME="$home/.local/state" \
    WAYLAND_DISPLAY="$parent_socket" \
    LANG="${LANG:-C.UTF-8}" \
    QT_FORCE_STDERR_LOGGING=1 \
    Hyprland --config "$config"
compositor_status=$?
set -e

# The command's output, whatever happened to the compositor afterwards. Hyprland is known to
# leave a nested session with a SIGSEGV during teardown, and the exit status of this script comes
# from the status file rather than from the compositor for exactly that reason.
if [[ -f "$output_file" ]]; then
    cat "$output_file"
fi

# The same three-way answer nested-session.sh gives: a command that ran and answered, a
# compositor that started and lost the command, and a compositor that never started at all.
if [[ ! -s "$status_file" ]]; then
    if [[ -e "$started_marker" ]]; then
        echo "hyprland-session.sh: the compositor started but the command left no exit status;" \
             "Hyprland exited with $compositor_status" >&2
        exit "$((compositor_status == 0 ? 1 : compositor_status))"
    fi
    echo "hyprland-session.sh: Hyprland exited with $compositor_status before it started a session;" \
         "the host cannot run a nested Hyprland, so this test is skipped" >&2
    exit "$SKIP_EXIT"
fi
exit "$(cat "$status_file")"

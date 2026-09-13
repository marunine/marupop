// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
//
// MaruPop cursor relay. Runs inside kwin_wayland's QJSEngine, in the compositor process, so
// the handler must never throw: Scripting::start() deletes a script whose evaluate() throws,
// and reports success over D-Bus while doing it.
//
// KWin exports no cursor position over D-Bus, and no Wayland protocol reports the pointer to a
// client that does not have it. workspace.cursorPosChanged is the one source, and it fires at
// the pointer's own event rate. To bound D-Bus traffic independently of the input device, the
// signal only sets a dirty flag and a QTimer does the sending.
//
// The reply drives the rate. MaruPop's Update returns true while it is tracking, which selects
// the 8 ms pump, and false otherwise, which selects the 500 ms heartbeat. A MaruPop that is not
// running makes the asynchronous call fail, which is dropped with no journal output, so the
// cost of an unused relay is 2 D-Bus calls per second.

var SERVICE = "io.github.marunine.marupop";
var OBJECT = "/Cursor";
var IFACE = "io.github.marunine.marupop.CursorSink";

var IDLE_MS = 500; // heartbeat while MaruPop is not tracking, or is not running
var ACTIVE_MS = 8; // 8 ms bounds the send rate at 125 Hz and the worst-case age at 8 ms

var dirty = false;
var lastX = -1;
var lastY = -1;
var active = false;

var pump = new QTimer();
pump.interval = IDLE_MS;

function setActive(on) {
    if (on === active) {
        return;
    }
    active = on;
    pump.interval = on ? ACTIVE_MS : IDLE_MS;
}

pump.timeout.connect(function () {
    var p = workspace.cursorPos;
    if (!dirty && active) {
        return;
    }
    if (active && p.x === lastX && p.y === lastY) {
        dirty = false;
        return;
    }
    dirty = false;
    lastX = p.x;
    lastY = p.y;
    var s = workspace.screenAt(p);
    // Six arguments plus the reply callback, which is seven of the nine slots callDBus takes.
    // Date.now() lets MaruPop measure the delivery latency and drop a stale sample.
    callDBus(SERVICE, OBJECT, IFACE, "Update",
             p.x, p.y,
             s ? s.name : "",
             s ? s.devicePixelRatio : 1.0,
             Date.now(),
             function (wantTracking) { setActive(wantTracking === true); });
});

workspace.cursorPosChanged.connect(function () { dirty = true; });
pump.start();

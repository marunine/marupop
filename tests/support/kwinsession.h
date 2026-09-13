// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// What the running KWin can serve a live capture case, read from the compositor itself.
//
// org.kde.KWin.ScreenShot2 renders the requested area through OpenGL, and KWin cancels every
// capture while it composites with QPainter. kwin_wayland --virtual composites with OpenGL only
// where a DRM render node (/dev/dri/renderD*) is present, so a container with no GPU starts the
// nested session, runs the layer-shell and scripting suites, and cancels every grab.
#pragma once

#include <QColor>
#include <QImage>
#include <QRect>
#include <QString>

namespace maru::test
{

// Grabs rect, in logical coordinates, through capture::KWinGrabber with includeOwnWindows set,
// which is the one way a suite sees its own surfaces: capture::KWinFrameSource sends
// hide-caller-windows. Runs the event loop until the grab answers or 15 s pass. A failure returns
// a null image and, where error is given, the message.
[[nodiscard]] QImage grabIncludingOwnWindows(const QRect &rect, QString *error = nullptr);

// True where every channel of the two colours differs by at most tolerance. The compositor can
// round a colour when it composites a surface and reads it back.
[[nodiscard]] bool coloursMatch(const QColor &left, const QColor &right, int tolerance = 4);

// The compositingType property of org.kde.kwin.Compositing at /Compositor on the session bus:
// "gl2" or "gles" for OpenGL, "qpainter" and "none" otherwise. Empty where org.kde.KWin does not
// answer.
[[nodiscard]] QString kwinCompositingType();

// The sentence a GTEST_SKIP() prints where the running KWin composites with a known type other
// than OpenGL, naming that type and the render node it needs. Empty where screenshots can work,
// and where the type could not be read: an unreadable property is a broken session, which the
// case itself reports as a failure rather than a skip.
[[nodiscard]] QString kwinScreenShotSkipReason();

} // namespace maru::test

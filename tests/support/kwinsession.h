// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// What the running KWin can serve a live capture case, read from the compositor itself.
//
// org.kde.KWin.ScreenShot2 renders the requested area through OpenGL, and KWin cancels every
// capture while it composites with QPainter. kwin_wayland --virtual composites with OpenGL only
// where a DRM render node (/dev/dri/renderD*) is present, so a container with no GPU starts the
// nested session, runs the layer-shell and scripting suites, and cancels every grab.
#pragma once

#include <QString>

namespace maru::test
{

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

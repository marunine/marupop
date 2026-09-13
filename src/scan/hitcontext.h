// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Everything the popup needs about one hit that the lookup response itself does not carry:
// where the pointer was, which output it was on, and where on the desktop the matched
// characters are. ScanController emits it beside every lookup::Response.
//
// Every rectangle here is in logical global desktop coordinates, the space QScreen::geometry()
// uses, already mapped out of the frame's image pixels by capture::imageToLogical(): nothing
// downstream of scan/ should have to know the scale a frame was grabbed at.
#pragma once

#include <QMetaType>
#include <QPoint>
#include <QRect>
#include <QString>

class QScreen;

namespace maru::scan
{

struct HitContext
{
    // The pointer position the hit was tested at, which is where the popup anchors the response.
    // It stays the position at the time of the test, because matchedRectLogical and
    // paragraphRectLogical below are computed in the coordinate space of that hit. Later pointer
    // samples reach the popup through scan::ScanController::hitMoved() instead, which carries a
    // position and no rectangle.
    QPoint cursorLogical;
    // The output under cursorLogical, or nullptr when the tracker reported none. The popup
    // needs it to place a layer-shell surface on the right output.
    QScreen *screen = nullptr;

    // The characters the response highlights, which is the span the popup points at.
    QRect matchedRectLogical;
    // The whole recognized reading unit the hit belongs to.
    QRect paragraphRectLogical;
    // The text of that reading unit, which is what the lookup ran over.
    QString paragraphText;
    // Index into paragraphText, in UTF-16 code units, of the character under the pointer. It
    // equals lookup::Response::highlightStart.
    qsizetype cursorIndex = 0;
    // True where the recognized text runs top to bottom, which the popup uses to decide which
    // side of the text it may cover.
    bool vertical = false;
    // The text-recognition backend that produced the paragraph, for the status line.
    QString backendName;

    // Wall-clock milliseconds of the stages behind this hit, for diagnostics.
    // grabMs and ocrMs are zero for a hit served from the recognition cache.
    qint64 grabMs = 0;
    qint64 ocrMs = 0;
    qint64 lookupMs = 0;
};

} // namespace maru::scan

Q_DECLARE_METATYPE(maru::scan::HitContext)

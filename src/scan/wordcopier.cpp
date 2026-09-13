// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "scan/wordcopier.h"

#include "core/logging.h"

#include <QClipboard>
#include <QMimeData>

#include <KSystemClipboard>

namespace maru::scan
{

namespace
{

// The characters the popup underlines, which is what MatchedText means where the response
// carries no result to read it off.
QString highlightedSpan(const lookup::Response &response, const HitContext &context)
{
    if (context.paragraphText.isEmpty() || response.highlightLength <= 0) {
        return {};
    }
    return context.paragraphText.mid(response.highlightStart, response.highlightLength);
}

} // namespace

QString wordToCopy(const lookup::Response &response, const HitContext &context, CopyWordMode mode)
{
    const lookup::Result *first = response.results.isEmpty() ? nullptr : &response.results.first();

    switch (mode) {
    case CopyWordMode::MatchedText:
        if (first != nullptr && !first->matchedText.isEmpty()) {
            return first->matchedText;
        }
        return highlightedSpan(response, context);
    case CopyWordMode::Headword:
        if (first == nullptr) {
            return highlightedSpan(response, context);
        }
        // A name entry can carry a reading and no spelling of its own, in which case the
        // headword the popup shows is the matched text.
        return first->primarySpelling.isEmpty() ? first->matchedText : first->primarySpelling;
    case CopyWordMode::Reading:
        if (first == nullptr) {
            return highlightedSpan(response, context);
        }
        if (!first->readings.isEmpty() && !first->readings.first().isEmpty()) {
            return first->readings.first();
        }
        // A kana headword has no separate reading: the spelling is the reading.
        return first->primarySpelling.isEmpty() ? first->matchedText : first->primarySpelling;
    }
    return {};
}

void copyToClipboard(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    KSystemClipboard *clipboard = KSystemClipboard::instance();
    if (clipboard == nullptr) {
        qCWarning(logScan) << "no system clipboard is available; the word was not copied";
        return;
    }
    // KSystemClipboard takes ownership of the mime data.
    auto *mime = new QMimeData;
    mime->setText(text);
    clipboard->setMimeData(mime, QClipboard::Clipboard);
    qCDebug(logScan) << "copied" << text.size() << "characters to the clipboard";
}

} // namespace maru::scan

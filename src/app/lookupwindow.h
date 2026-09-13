// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The lookup window: the sentence of the last hit, the term matched in it, and the results of
// that lookup in a resizable top-level window.
#pragma once

#include "lookup/lookuptypes.h"

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QTextBrowser;

namespace maru::popup
{
class PopupView;
}

namespace maru::scan
{
struct HitContext;
}

namespace maru
{

class LookupCard;

// One sentence of a recognized paragraph, and the span of the matched term inside it.
struct SentenceSpan
{
    QString text;
    // Index into text, in UTF-16 code units. termLength is 0 where the term lies outside text,
    // which is a term starting on a bracket jp::findSentence() strips.
    qsizetype termStart = 0;
    qsizetype termLength = 0;
};

// The sentence jp::findSentence() answers around the term at start in paragraph, with the term
// located inside it. jp::findSentence() answers a contiguous slice of paragraph, so the
// occurrence of that slice covering start is the one it was taken from.
[[nodiscard]] SentenceSpan sentenceAround(const QString &paragraph, qsizetype start, qsizetype length);

// Yomitan's search page (ext/search.html) is the model: the query text above a scrollable list of
// entries. The sentence sits in a selectable text view with the term in bold and underlined, a
// button copies the sentence, and the results are a PopupView on a card painted in the popup
// theme, which scrolls on the wheel. The splitter between the two moves the space the sentence
// takes.
//
// The window lists LookupWindowMaxResults results where the popup lists MaxResults. Application
// raises the scan's result count to the larger of the two while the window is open, through
// scan::ScanController::setMinimumResults(), and lookup::firstResults() cuts that one answer for
// each view.
//
// The grab around the pointer includes this window whenever the pointer is over it, and the
// window's own text would then replace its content. setLookup() is therefore dropped while the
// pointer is inside the window, which is the state isFrozen() reports.
class LookupWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LookupWindow(QWidget *parent = nullptr);

    // Shows response and the sentence it was looked up in. context.paragraphText is the text the
    // response's highlight indexes. Dropped while isFrozen() holds.
    void setLookup(const lookup::Response &response, const scan::HitContext &context);
    [[nodiscard]] bool isFrozen() const;

    [[nodiscard]] SentenceSpan sentence() const;
    // The source characters the first result matched, which the header line names.
    [[nodiscard]] QString term() const;
    // The view listing the first LookupWindowMaxResults results.
    [[nodiscard]] popup::PopupView *resultView() const;

    // Re-reads LookupWindowMaxResults, the PopupAppearance group and the PopupContent group, and
    // re-renders the response in hand.
    void applySettings();

    // Writes the sentence to the clipboard through scan::copyToClipboard(). An empty sentence
    // writes nothing.
    void copySentence();

Q_SIGNALS:
    // Emitted from a show and a hide the application requested; a minimize carries no signal.
    void visibilityChanged(bool visible);
    void sentenceCopied(const QString &sentence);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void render();

    QLabel *m_termLabel = nullptr;
    QPushButton *m_copyButton = nullptr;
    QTextBrowser *m_sentenceView = nullptr;
    LookupCard *m_card = nullptr;
    popup::PopupView *m_view = nullptr;
    QLabel *m_placeholder = nullptr;

    // Every result of the lookup, uncut, so a raised LookupWindowMaxResults re-renders from it.
    lookup::Response m_response;
    SentenceSpan m_sentence;
    QString m_term;
    bool m_pointerInside = false;
};

} // namespace maru

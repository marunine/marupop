// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popup/popupview.h"

#include "popup/pitchpainter.h"
#include "popup/renderer.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <cmath>

namespace maru::popup
{

PopupView::PopupView(QWidget *parent)
    : QTextBrowser{parent}
{
    setFrameShape(QFrame::NoFrame);
    setLineWidth(0);
    setOpenExternalLinks(false);
    setOpenLinks(false);
    setTextInteractionFlags(Qt::NoTextInteraction);
    setFocusPolicy(Qt::NoFocus);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // The card background is painted by the parent, so both the frame and the viewport stay
    // transparent. A style sheet would reach the document's default style as well, which is
    // why the transparency is set through the palette.
    viewport()->setAutoFillBackground(false);
    QPalette transparent = palette();
    transparent.setColor(QPalette::Base, Qt::transparent);
    transparent.setColor(QPalette::Window, Qt::transparent);
    setPalette(transparent);

    // The card supplies its own padding, so the document adds none.
    document()->setDocumentMargin(0);
    setTheme(m_theme);
    applyScrollBarStyle();
}

const PopupModel &PopupView::model() const
{
    return m_model;
}

void PopupView::setModel(const PopupModel &model)
{
    m_model = model;
    rerender();
}

void PopupView::setTheme(const Theme &theme)
{
    m_theme = theme;
    QFont content = font();
    if (!theme.fontFamily.isEmpty()) {
        content.setFamily(theme.fontFamily);
    }
    content.setPointSize(theme.definitionPt);
    setFont(content);
    applyScrollBarStyle();
    rerender();
}

void PopupView::setRenderOptions(const RenderOptions &options)
{
    m_options = options;
    rerender();
}

QString PopupView::plainText() const
{
    return m_plainText;
}

void PopupView::rerender()
{
    m_plainText = renderPlainText(m_model, m_options);
    setHtml(renderHtml(m_model, m_options, m_theme));
    applyLineHeight(*document(), m_theme.lineHeightPercent);
    viewport()->update();
}

void applyLineHeight(QTextDocument &document, int percent)
{
    if (percent == 100) {
        return;
    }
    QTextCursor cursor{&document};
    cursor.beginEditBlock();
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        if (format.lineHeightType() != QTextBlockFormat::SingleHeight) {
            continue;
        }
        format.setLineHeight(percent, QTextBlockFormat::ProportionalHeight);
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
    }
    cursor.endEditBlock();
}

QSizeF PopupView::layoutContent(int maxContentWidth)
{
    QTextDocument *document = this->document();
    const qreal limit = qMax(1, maxContentWidth);
    document->setTextWidth(limit);
    const qreal ideal = document->idealWidth();
    if (ideal < limit) {
        document->setTextWidth(std::ceil(ideal));
    }
    return document->size();
}

void PopupView::applyScrollBarStyle()
{
    // The style sheet is set on the scroll bar rather than on the view: a style sheet on a
    // QTextBrowser switches the whole widget to QStyleSheetStyle, which repaints the viewport
    // with the palette's own background and hides the card behind it.
    const QColor handle = blend(m_theme.foreground, m_theme.background, 0.45);
    verticalScrollBar()->setStyleSheet(
        QStringLiteral("QScrollBar:vertical { background: transparent; width: 8px; margin: 0px; }"
                       "QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 24px; }"
                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
                       "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
            .arg(handle.name(QColor::HexRgb)));
}

void PopupView::setScrollable(bool scrollable)
{
    m_scrollable = scrollable;
    setVerticalScrollBarPolicy(scrollable ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    setTextInteractionFlags(scrollable ? Qt::TextBrowserInteraction : Qt::NoTextInteraction);
    setFocusPolicy(scrollable ? Qt::StrongFocus : Qt::NoFocus);
}

bool PopupView::isScrollable() const
{
    return m_scrollable;
}

void PopupView::paintEvent(QPaintEvent *event)
{
    QTextBrowser::paintEvent(event);
    if (!m_options.showPitchAccent || m_model.entries.isEmpty()) {
        return;
    }

    QFont readingFont = font();
    readingFont.setPointSize(qMax(6, m_theme.headerPt - 2));
    // renderHtml() emits the reading two points below the headword, which is the size the
    // contour is measured and positioned against.
    const QFontMetricsF readingMetrics{readingFont};

    QPainter painter{viewport()};
    painter.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());

    const QList<ReadingSpan> spans = readingSpans(*document());
    for (const ReadingSpan &span : spans) {
        if (span.entryIndex < 0 || span.entryIndex >= m_model.entries.size()) {
            continue;
        }
        const Entry &entry = m_model.entries.at(span.entryIndex);
        if (span.readingIndex < 0 || span.readingIndex >= entry.pitchPositions.size()) {
            continue;
        }
        const std::optional<quint8> position = entry.pitchPositions.at(span.readingIndex);
        if (!position.has_value()) {
            continue;
        }
        // A reading wrapped across a line break yields one span per line, and a contour cannot
        // span a line break, so a partial span is left undrawn.
        if (span.reading != entry.readings.value(span.readingIndex)) {
            continue;
        }
        // The contour spans the reading's own ascent above the baseline: the line box is as
        // tall as the headword beside the reading, which would put the low level well below
        // the kana.
        const QRectF contour{
            span.rect.left(), span.baseline - readingMetrics.ascent(), span.rect.width(), readingMetrics.ascent()};
        paintPitch(painter, contour, span.reading, *position, readingFont, m_theme.highlightReading, false);
    }
}

} // namespace maru::popup

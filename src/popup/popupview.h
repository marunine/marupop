// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The content view of the popup card, shared by PopupWindow and PopupPreview.
#pragma once

#include "popup/entrymodel.h"
#include "popup/renderoptions.h"
#include "popup/theme.h"

#include <QTextBrowser>

namespace maru::popup
{

// A read-only, frameless, transparent QTextBrowser carrying one rendered PopupModel, plus the
// pitch-accent contours painted over the reading runs of the document.
//
// QTextBrowser rather than QLabel, which is what meikipop uses: a QLabel has no viewport to
// scroll, and the pin mode of PopupWindow needs one.
class PopupView : public QTextBrowser
{
    Q_OBJECT

public:
    explicit PopupView(QWidget *parent = nullptr);

    // Re-renders with the theme and the options in hand.
    void setModel(const PopupModel &model);
    [[nodiscard]] const PopupModel &model() const;

    // Both re-render the model in hand. setTheme() also applies the font family and the
    // definition size to the widget, which is what the document inherits.
    void setTheme(const Theme &theme);
    void setRenderOptions(const RenderOptions &options);

    // The plain-text rendering of the model in hand, for the clipboard.
    [[nodiscard]] QString plainText() const;

    // Lays the document out at a text width of at most maxContentWidth and returns the size it
    // takes. The width returned is the ideal width when the content is narrower than
    // maxContentWidth.
    QSizeF layoutContent(int maxContentWidth);

    // Shows the vertical scroll bar when the content overflows, and enables wheel scrolling.
    void setScrollable(bool scrollable);
    [[nodiscard]] bool isScrollable() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rerender();
    void applyScrollBarStyle();

    PopupModel m_model;
    Theme m_theme;
    RenderOptions m_options;
    QString m_plainText;
    bool m_scrollable = false;
};

// Sets a proportional line height of percent on every block of document whose line-height type
// is QTextBlockFormat::SingleHeight, table cells included. A block carrying a line-height of its
// own, such as one from a Yomitan glossary's `lineHeight` style, keeps that value. The walk is
// per block because QTextDocument::setHtml() applies a CSS line-height on a <div> to the first
// block inside it alone. 100 leaves the document unchanged.
void applyLineHeight(QTextDocument &document, int percent);

} // namespace maru::popup

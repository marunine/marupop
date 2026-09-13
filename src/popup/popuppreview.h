// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The live preview of the popup card shown on the Popup Appearance page of the settings
// dialog.
#pragma once

#include "popup/entrymodel.h"
#include "popup/renderoptions.h"
#include "popup/theme.h"

#include <QWidget>

namespace maru::popup
{

class PopupView;

// A fixed sample model: one verb entry carrying a deconjugation path, a frequency rank and two
// senses; one noun entry carrying two readings with pitch positions; and the kanji card of 本.
// The preview and the tools/popupprobe.cpp probe both render it.
[[nodiscard]] PopupModel samplePopupModel();

// The card, drawn as an ordinary child widget rather than as a layer surface, so the settings
// dialog can embed it. The theme and the options are read from marupoprc by applyTheme() and
// applyRenderOptions(), which the dialog calls whenever a widget on the page changes.
class PopupPreview : public QWidget
{
    Q_OBJECT

public:
    explicit PopupPreview(QWidget *parent = nullptr);

    // Replaces samplePopupModel() with another model.
    void setModel(const PopupModel &model);

    void applyTheme();
    void applyRenderOptions();

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    PopupView *m_view = nullptr;
    Theme m_theme;
    RenderOptions m_options;
};

} // namespace maru::popup

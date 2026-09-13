// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <QKeySequence>
#include <QPushButton>

class KKeySequenceRecorder;

namespace maru
{

// A button that records a global shortcut when clicked, built on KKeySequenceRecorder so the
// settings dialog does not need KXmlGui's KKeySequenceWidget. Recording grabs the keyboard
// until a full combination arrives; Escape cancels and leaves the previous sequence in place.
//
// Copied from marusnap/src/app/shortcutbutton.{h,cpp} (LGPL-3.0, same author), with the bare-key
// pattern dropped: every shortcut this records is global.
class ShortcutButton : public QPushButton
{
    Q_OBJECT

public:
    explicit ShortcutButton(QWidget *parent = nullptr);

    void setKeySequence(const QKeySequence &sequence);
    [[nodiscard]] QKeySequence keySequence() const;
    void clearKeySequence();

Q_SIGNALS:
    void keySequenceChanged(const QKeySequence &sequence);

private:
    void updateText();

    KKeySequenceRecorder *m_recorder = nullptr;
    QKeySequence m_sequence;
};

} // namespace maru

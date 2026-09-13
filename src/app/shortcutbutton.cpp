// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/shortcutbutton.h"

#include <QWindow>

#include <KKeySequenceRecorder>
#include <KLocalizedString>

namespace maru
{

ShortcutButton::ShortcutButton(QWidget *parent)
    : QPushButton(parent)
    , m_recorder(new KKeySequenceRecorder(nullptr, this))
{
    setCheckable(true);
    setIcon(QIcon::fromTheme(QStringLiteral("configure-shortcuts")));
    updateText();

    connect(this, &QPushButton::clicked, this, [this] {
        if (m_recorder->isRecording()) {
            return;
        }
        // The recorder needs a window to grab the keyboard on, and this one only exists once
        // the dialog is shown.
        m_recorder->setWindow(window()->windowHandle());
        // ModifierAndKey alone: these are global shortcuts, and a bare key bound globally
        // swallows that key in every application on the desktop.
        m_recorder->setPatterns(KKeySequenceRecorder::ModifierAndKey);
        m_recorder->setCurrentKeySequence({});
        m_recorder->startRecording();
        setText(i18nc("@info:placeholder while recording a shortcut", "Press a shortcut"));
    });
    connect(m_recorder, &KKeySequenceRecorder::gotKeySequence, this, [this](const QKeySequence &sequence) {
        setChecked(false);
        if (!sequence.isEmpty() && sequence != m_sequence) {
            m_sequence = sequence;
            Q_EMIT keySequenceChanged(m_sequence);
        }
        updateText();
    });
    connect(m_recorder, &KKeySequenceRecorder::recordingChanged, this, [this] {
        if (!m_recorder->isRecording()) {
            setChecked(false);
            updateText();
        }
    });
}

void ShortcutButton::setKeySequence(const QKeySequence &sequence)
{
    if (m_sequence == sequence) {
        return;
    }
    m_sequence = sequence;
    updateText();
}

QKeySequence ShortcutButton::keySequence() const
{
    return m_sequence;
}

void ShortcutButton::clearKeySequence()
{
    if (m_sequence.isEmpty()) {
        return;
    }
    m_sequence = QKeySequence{};
    updateText();
    Q_EMIT keySequenceChanged(m_sequence);
}

void ShortcutButton::updateText()
{
    setText(m_sequence.isEmpty() ? i18nc("@action:button no shortcut is bound", "None")
                                 : m_sequence.toString(QKeySequence::NativeText));
}

} // namespace maru

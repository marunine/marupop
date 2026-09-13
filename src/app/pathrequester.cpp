// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/pathrequester.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>

#include <KLocalizedString>

namespace maru
{

PathRequester::PathRequester(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_kind(kind)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    m_edit->setClearButtonEnabled(true);
    layout->addWidget(m_edit, 1);

    auto *browse = new QToolButton(this);
    browse->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    browse->setToolTip(kind == Kind::Directory ? i18nc("@info:tooltip", "Select a folder")
                                               : i18nc("@info:tooltip", "Select a file"));
    browse->setAccessibleName(browse->toolTip());
    layout->addWidget(browse);
    connect(browse, &QToolButton::clicked, this, &PathRequester::browse);

    setFocusProxy(m_edit);
    connect(m_edit, &QLineEdit::textChanged, this, &PathRequester::pathChanged);
}

QString PathRequester::path() const
{
    return m_edit->text();
}

void PathRequester::setPath(const QString &path)
{
    if (m_edit->text() != path) {
        m_edit->setText(path);
    }
}

void PathRequester::setPlaceholderText(const QString &text)
{
    m_edit->setPlaceholderText(text);
}

void PathRequester::browse()
{
    const QString current = m_edit->text();
    // A relative entry (a bare command name, say) is not a useful starting directory.
    const QString start = QFileInfo{current}.isAbsolute() ? current : QString();
    const QString chosen = m_kind == Kind::Directory
                               ? QFileDialog::getExistingDirectory(this, i18nc("@title:window", "Select Folder"), start)
                               : QFileDialog::getOpenFileName(
                                     this, i18nc("@title:window", "Select File"), QFileInfo{start}.absolutePath());
    if (!chosen.isEmpty()) {
        m_edit->setText(chosen);
    }
}

} // namespace maru

// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/pathrequester.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
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

    m_browse = new QToolButton(this);
    m_browse->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    layout->addWidget(m_browse);
    connect(m_browse, &QToolButton::clicked, this, &PathRequester::browse);
    updateBrowseButton();

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

void PathRequester::setKind(Kind kind)
{
    m_kind = kind;
    updateBrowseButton();
}

void PathRequester::setNameFilters(const QStringList &filters)
{
    m_nameFilters = filters;
}

void PathRequester::updateBrowseButton()
{
    switch (m_kind) {
    case Kind::Directory:
        m_browse->setToolTip(i18nc("@info:tooltip", "Select a folder"));
        break;
    case Kind::File:
        m_browse->setToolTip(i18nc("@info:tooltip", "Select a file"));
        break;
    case Kind::FileOrDirectory:
        m_browse->setToolTip(i18nc("@info:tooltip", "Select a file or a folder"));
        break;
    }
    m_browse->setAccessibleName(m_browse->toolTip());
}

void PathRequester::browse()
{
    switch (m_kind) {
    case Kind::Directory:
        browseDirectory();
        return;
    case Kind::File:
        browseFile();
        return;
    case Kind::FileOrDirectory: {
        QMenu menu{this};
        menu.addAction(i18nc("@action:inmenu", "Select File…"), this, &PathRequester::browseFile);
        menu.addAction(i18nc("@action:inmenu", "Select Folder…"), this, &PathRequester::browseDirectory);
        menu.exec(m_browse->mapToGlobal(QPoint{0, m_browse->height()}));
        return;
    }
    }
}

void PathRequester::browseFile()
{
    const QString current = m_edit->text();
    // A relative entry (a bare command name, say) is not a useful starting directory.
    const QString start = QFileInfo{current}.isAbsolute() ? QFileInfo{current}.absolutePath() : QString();
    const QString chosen = QFileDialog::getOpenFileName(
        this, i18nc("@title:window", "Select File"), start, m_nameFilters.join(QStringLiteral(";;")));
    if (!chosen.isEmpty()) {
        m_edit->setText(chosen);
    }
}

void PathRequester::browseDirectory()
{
    const QString current = m_edit->text();
    const QString start = QFileInfo{current}.isAbsolute() ? current : QString();
    const QString chosen = QFileDialog::getExistingDirectory(this, i18nc("@title:window", "Select Folder"), start);
    if (!chosen.isEmpty()) {
        m_edit->setText(chosen);
    }
}

} // namespace maru

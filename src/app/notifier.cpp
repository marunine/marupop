// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/notifier.h"

#include "core/settings.h"

#include <QLatin1StringView>

#include <KLocalizedString>
#include <KNotification>

#include <array>

namespace maru
{

namespace
{

// Indexed by Notifier::Event. The static_assert is what keeps a newly added event from
// reaching KNotification without an id -- and so without a notifyrc group to present it.
constexpr std::array kEventIds{
    QLatin1StringView("dictionaryImported"),
    QLatin1StringView("dictionaryUpdateAvailable"),
    QLatin1StringView("modelsDownloaded"),
    QLatin1StringView("failed"),
};
static_assert(kEventIds.size() == static_cast<std::size_t>(Notifier::Event::Count),
              "every Notifier::Event needs a notifyrc event id");

} // namespace

QStringList Notifier::eventIds()
{
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(kEventIds.size()));
    for (const QLatin1StringView id : kEventIds) {
        ids.append(QString{id});
    }
    return ids;
}

bool Notifier::isEnabled(Event event)
{
    if (event == Event::Failed) {
        return PopSettings::notifyOnError();
    }
    return true;
}

Notifier::Notifier(QObject *parent)
    : QObject(parent)
{}

void Notifier::dictionaryImported(const QString &name, int entries)
{
    notify(Event::DictionaryImported,
           i18nc("@info:notification title", "Dictionary Imported"),
           i18ncp("@info:notification", "%2: %1 entry", "%2: %1 entries", entries, name),
           QStringLiteral("accessories-dictionary"));
}

void Notifier::dictionaryUpdateAvailable(const QString &name)
{
    notify(Event::DictionaryUpdateAvailable,
           i18nc("@info:notification title", "Dictionary Update Available"),
           i18nc("@info:notification", "An update for %1 is available.", name),
           QStringLiteral("update-none"));
}

void Notifier::modelsDownloaded()
{
    notify(Event::ModelsDownloaded,
           i18nc("@info:notification title", "Recognition Models Downloaded"),
           i18nc("@info:notification", "Text recognition models downloaded and verified."),
           QStringLiteral("document-scan"));
}

void Notifier::failure(const QString &title, const QString &message)
{
    notify(Event::Failed, title, message, QStringLiteral("dialog-error"));
}

void Notifier::notify(Event event, const QString &title, const QString &text, const QString &iconName)
{
    if (!isEnabled(event)) {
        return;
    }
    auto *notification =
        new KNotification(QString{kEventIds.at(static_cast<std::size_t>(event))}, KNotification::CloseOnTimeout);
    // The component name is the notifyrc file name, which stays marupop: renaming it would take
    // marupoprc and ~/.config/marupop with it.
    notification->setComponentName(QStringLiteral("marupop"));
    notification->setTitle(title);
    // The body is markup -- Plasma parses it as rich text and swallows whatever looks like a
    // tag -- so the parts we do not control (dictionary names, error strings) have to be
    // escaped or they arrive mangled. The title is not escaped: the summary is shown as plain
    // text, and an escaped one would read "a &amp; b".
    notification->setText(text.toHtmlEscaped());
    notification->setIconName(iconName);
    notification->sendEvent();
}

} // namespace maru

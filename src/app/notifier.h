// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace maru
{

// KNotification events from data/marupop.notifyrc. The notifyrc carries presentation defaults
// alone; the application's own opt-in is NotifyOnError, which gates the failure channel and
// defaults to true. The three events that announce finished work are raised only for work the
// user asked for, so they carry no setting of their own. Individual events can still be
// re-tuned in System Settings.
class Notifier : public QObject
{
    Q_OBJECT

public:
    // Every event the class can raise. Each one needs an [Event/<id>] group in the notifyrc
    // whose Action names a presentation: KNotification throws away an event configured with
    // an empty (or None) Action before it ever reaches the notification server, so the toast
    // silently never appears. Count keeps the id table in notifier.cpp complete.
    enum class Event
    {
        DictionaryImported,
        DictionaryUpdateAvailable,
        ModelsDownloaded,
        Failed,
        Count,
    };

    // The notifyrc group ids, in Event order, so the file can be checked against them.
    [[nodiscard]] static QStringList eventIds();

    // Whether the setting covering this event lets it through. Failed reads NotifyOnError;
    // every other event is unconditional.
    [[nodiscard]] static bool isEnabled(Event event);

    explicit Notifier(QObject *parent = nullptr);

    void dictionaryImported(const QString &name, int entries);
    void dictionaryUpdateAvailable(const QString &name);
    void modelsDownloaded();
    // Failures never open a modal dialog; they go to the failed channel and the tray tooltip.
    void failure(const QString &title, const QString &message);

private:
    void notify(Event event, const QString &title, const QString &text, const QString &iconName);
};

} // namespace maru

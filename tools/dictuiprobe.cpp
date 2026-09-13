// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Opens the Manage Dictionaries dialog on the live session against a throwaway dictionary
// directory, which is the only way to see the tree's drag reordering, the icon-theme decorations,
// the message widget's action buttons and the progress row: the offscreen test platform draws
// none of them.
//
// Build: cmake -B build-dictui -DMARUPOP_BUILD_DEV_TOOLS=ON && cmake --build build-dictui
//        --target marupop-dictuiprobe
// Run:   MARUPOP_DICTUI_HOME=/tmp/dictui ./build-dictui/tools/marupop-dictuiprobe [folder…]
//
// Each positional argument is a Yomitan folder or .zip added to the list before the dialog opens,
// classified by YomitanImporter::detectTypes(). MARUPOP_DICTUI_HOME names the directory the list
// and the stores are written to; without it a temporary directory is used and removed on exit.
#include "dict/dictionarymanager.h"
#include "dict/importers/yomitanimporter.h"
#include "dictui/addcustomentrydialog.h"
#include "dictui/adddictionarydialog.h"
#include "dictui/configuredictionarydialog.h"
#include "dictui/dictionarymanagerdialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QTemporaryDir>

#include <KLocalizedString>

#include <cstdio>
#include <memory>

using namespace maru;

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("marunine.github.io"));
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("marupop"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Opens the MaruPop Manage Dictionaries dialog against a scratch directory."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("source"),
                                 QStringLiteral("A Yomitan dictionary folder or .zip to add to the list."),
                                 QStringLiteral("[source…]"));
    const QCommandLineOption renderOption{
        QStringLiteral("render"),
        QStringLiteral("Render the manager dialog and each sub-dialog into <dir> as PNG files and exit, instead of "
                       "waiting for the window to be closed. The session's own style and icon theme are used, so "
                       "this is a screenshot of the real thing without taking one of the screen."),
        QStringLiteral("dir")};
    parser.addOption(renderOption);
    const QCommandLineOption importOption{
        QStringLiteral("import"),
        QStringLiteral("Import every dictionary in the list before rendering, so the rows show record counts.")};
    parser.addOption(importOption);
    const QCommandLineOption seedOption{QStringLiteral("seed"),
                                        QStringLiteral("Seed the built-in JMdict, KANJIDIC2, JMnedict and kanji "
                                                       "component entries, so the download menu has content.")};
    parser.addOption(seedOption);
    parser.process(app);

    QString home = qEnvironmentVariable("MARUPOP_DICTUI_HOME");
    std::unique_ptr<QTemporaryDir> temporary;
    if (home.isEmpty()) {
        temporary = std::make_unique<QTemporaryDir>();
        home = temporary->path();
    }
    QDir().mkpath(home);
    std::printf("dictionary directory: %s\n", qPrintable(home));

    dict::DictionaryManager manager(home, nullptr);
    if (!manager.load())
        std::fprintf(stderr, "the existing dictionaries.json could not be read\n");
    if (parser.isSet(seedOption))
        manager.seedBuiltIns();

    const QStringList sources = parser.positionalArguments();
    for (const QString &source : sources) {
        if (!QFileInfo::exists(source)) {
            std::fprintf(stderr, "no such source: %s\n", qPrintable(source));
            continue;
        }
        const QList<dict::DictType> detected = dict::YomitanImporter::detectTypes(source);
        if (detected.isEmpty()) {
            std::fprintf(stderr, "no Yomitan bank files in %s\n", qPrintable(source));
            continue;
        }
        const dict::YomitanIndex index = dict::YomitanImporter::readIndex(source);

        dict::Dictionary entry;
        entry.type = detected.constFirst();
        entry.name = index.valid && !index.title.isEmpty() ? index.title : QFileInfo(source).completeBaseName();
        entry.sourcePath = source;
        entry.revision = index.revision;
        entry.autoUpdatable = index.isUpdatable;
        if (manager.dictionaryNamed(entry.name) != nullptr)
            continue;
        manager.add(entry);
    }
    (void)manager.save();

    DictionaryManagerDialog dialog(manager);
    QObject::connect(&dialog, &DictionaryManagerDialog::dictionaryImported, [&manager](const QUuid &id) {
        const dict::Dictionary *entry = manager.dictionary(id);
        std::printf("imported %s: %lld records\n",
                    qPrintable(entry != nullptr ? entry->name : QString()),
                    entry != nullptr ? static_cast<long long>(entry->recordCount) : 0LL);
    });
    QObject::connect(&dialog, &DictionaryManagerDialog::importFailed, [](const QUuid &, const QString &message) {
        std::printf("import failed: %s\n", qPrintable(message));
    });
    if (!parser.isSet(renderOption)) {
        dialog.exec();
        return 0;
    }

    const QString outputDirectory = parser.value(renderOption);
    QDir().mkpath(outputDirectory);
    dialog.show();

    const auto settle = [](int milliseconds) {
        QDeadlineTimer deadline(milliseconds);
        while (!deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    };
    const auto render = [&outputDirectory](QWidget *widget, const QString &name) {
        const QString path = outputDirectory + QLatin1Char('/') + name + QLatin1String(".png");
        if (widget->grab().save(path))
            std::printf("wrote %s\n", qPrintable(path));
        else
            std::fprintf(stderr, "could not write %s\n", qPrintable(path));
    };

    if (parser.isSet(importOption)) {
        const QList<dict::Dictionary *> pending = manager.dictionaries();
        for (const dict::Dictionary *entry : pending) {
            if (!entry->sourcePath.isEmpty())
                dialog.importDictionary(entry->id);
        }
        QDeadlineTimer deadline(300000);
        while (dialog.isBusy() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    settle(600);
    render(&dialog, QStringLiteral("manager"));

    AddDictionaryDialog add(manager);
    add.selectFormat(AddDictionaryDialog::Format::Yomitan);
    add.show();
    settle(300);
    render(&add, QStringLiteral("add"));

    const QList<dict::Dictionary *> entries = manager.dictionaries();
    for (const dict::Dictionary *entry : entries) {
        if (entry->type != dict::DictType::JMdict)
            continue;
        ConfigureDictionaryDialog configure(manager, entry->id);
        configure.show();
        settle(300);
        render(&configure, QStringLiteral("configure-jmdict"));
        break;
    }
    for (const dict::Dictionary *entry : entries) {
        if (entry->builtIn || entry->sourcePath.isEmpty())
            continue;
        ConfigureDictionaryDialog configure(manager, entry->id);
        configure.show();
        settle(300);
        render(&configure, QStringLiteral("configure-yomitan"));
        break;
    }

    AddCustomEntryDialog addWord(manager, AddCustomEntryDialog::Mode::Word);
    addWord.show();
    settle(300);
    render(&addWord, QStringLiteral("add-word"));

    AddCustomEntryDialog addName(manager, AddCustomEntryDialog::Mode::Name);
    addName.show();
    settle(300);
    render(&addName, QStringLiteral("add-name"));
    return 0;
}

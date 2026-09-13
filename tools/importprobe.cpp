// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Drives maru::dict::DictionaryManager from a terminal against the application's own dictionary
// directory, which is what fills a real installation without the Manage Dictionaries dialog and
// without a download. The dialog covers the same operations, but a headless run is what a
// timing measurement and a scripted first-time setup need.
//
// Build: cmake -B build -DMARUPOP_BUILD_DEV_TOOLS=ON && cmake --build build --target
//        marupop-importprobe
// Run:   ./build/bin/marupop-importprobe --list
//        ./build/bin/marupop-importprobe --seed --set-source JMdict /path/JMdict.xml --import JMdict
//        ./build/bin/marupop-importprobe --add-yomitan '/path/[Pitch] NHK2016' --import 'NHK2016'
//
// MARUPOP_DICT_HOME overrides the directory, so a run can be pointed at a scratch copy rather
// than at paths::dictionariesDir().
#include "core/paths.h"
#include "dict/dictionary.h"
#include "dict/dictionaryimportjob.h"
#include "dict/dictionarymanager.h"
#include "dict/importers/yomitanimporter.h"
#include "dict/store.h"
#include "ocr/modelstore.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QUuid>

#include <KLocalizedString>

#include <algorithm>
#include <cstdio>

using namespace maru;

namespace
{

// A dictionary named on the command line, by display name or by id. The name is what a user
// reads off --list; the id is what a script that just created an entry has.
dict::Dictionary *resolve(dict::DictionaryManager &manager, const QString &nameOrId)
{
    if (dict::Dictionary *byName = manager.dictionaryNamed(nameOrId))
        return byName;
    const QUuid id(nameOrId);
    return id.isNull() ? nullptr : manager.dictionary(id);
}

QString typeName(dict::DictType type)
{
    return dict::dictTypeName(type);
}

// The type a --as value names. The spelling is the enum's own, lowercased, so the list does not
// have to be repeated in a help string that would drift from it.
bool parseType(const QString &text, dict::DictType *type)
{
    static const QList<std::pair<QLatin1StringView, dict::DictType>> names = {
        {QLatin1StringView("word"), dict::DictType::YomitanWord},
        {QLatin1StringView("kanji"), dict::DictType::YomitanKanji},
        {QLatin1StringView("kanjiwordschema"), dict::DictType::YomitanKanjiWordSchema},
        {QLatin1StringView("name"), dict::DictType::YomitanName},
        {QLatin1StringView("pitch"), dict::DictType::YomitanPitchAccent},
        {QLatin1StringView("other"), dict::DictType::YomitanOther},
        {QLatin1StringView("frequency"), dict::DictType::YomitanFrequency},
        {QLatin1StringView("kanjifrequency"), dict::DictType::YomitanKanjiFrequency},
    };
    const auto match = std::ranges::find_if(names, [&text](const auto &entry) {
        return text.compare(entry.first, Qt::CaseInsensitive) == 0;
    });
    if (match == names.cend()) {
        return false;
    }
    *type = match->second;
    return true;
}

// The (name, path) pairs that follow each --set-source. QCommandLineParser takes one value per
// option occurrence, so the second token would land among the positional arguments and lose the
// pairing; reading the raw list keeps the two together.
QList<std::pair<QString, QString>> setSourcePairs(const QStringList &arguments)
{
    QList<std::pair<QString, QString>> pairs;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        if (arguments.at(i) != QLatin1StringView("--set-source"))
            continue;
        if (i + 2 >= arguments.size()) {
            std::fprintf(stderr, "--set-source takes a name and a path\n");
            break;
        }
        pairs.append({arguments.at(i + 1), arguments.at(i + 2)});
        i += 2;
    }
    return pairs;
}

void printList(dict::DictionaryManager &manager)
{
    const QList<dict::Dictionary *> entries = manager.dictionaries();
    std::printf("%-3s %-38s %-22s %-10s %10s %10s  %s\n", "#", "name", "type", "state", "records", "keys", "source");
    for (const dict::Dictionary *entry : entries) {
        const char *state = "pending";
        if (entry->importedAt.isValid()) {
            state = "imported";
        } else if (entry->sourcePath.isEmpty()) {
            state = "-";
        }
        std::printf("%-3d %-38s %-22s %-10s %10lld %10lld  %s\n",
                    entry->priority,
                    qPrintable(entry->name),
                    qPrintable(typeName(entry->type)),
                    entry->enabled ? state : "disabled",
                    static_cast<long long>(entry->recordCount),
                    static_cast<long long>(entry->keyCount),
                    qPrintable(entry->sourcePath));
    }
}

// Runs one import to completion on a nested event loop, reporting the percentage on stderr so a
// piped run still shows progress while stdout carries the result line alone.
bool runImport(dict::DictionaryManager &manager, dict::Dictionary *entry)
{
    dict::DictionaryImportJob *job = manager.createImportJob(entry->id);
    if (job == nullptr) {
        std::fprintf(
            stderr, "no importer for %s (source %s)\n", qPrintable(entry->name), qPrintable(entry->sourcePath));
        return false;
    }
    const QString name = entry->name;
    const QUuid id = entry->id;

    QEventLoop loop;
    int lastPercent = -1;
    QObject::connect(
        job, &dict::DictionaryImportJob::progress, &loop, [&lastPercent, name](int percent, const QString &message) {
            if (percent == lastPercent)
                return;
            lastPercent = percent;
            std::fprintf(stderr, "\r%s: %3d%% %-40s", qPrintable(name), percent, qPrintable(message));
            std::fflush(stderr);
        });
    QObject::connect(job, &KJob::result, &loop, &QEventLoop::quit);

    QElapsedTimer timer;
    timer.start();
    job->start();
    loop.exec();
    const qint64 elapsed = timer.elapsed();
    std::fprintf(stderr, "\r%-60s\r", "");

    if (job->error() != 0) {
        std::fprintf(stderr, "%s: import failed: %s\n", qPrintable(name), qPrintable(job->errorString()));
        return false;
    }
    if (!manager.applyImportResult(id, job)) {
        std::fprintf(stderr, "%s: the import result could not be applied\n", qPrintable(name));
        return false;
    }
    const dict::Dictionary *imported = manager.dictionary(id);
    const QString databasePath = dict::databasePathFor(manager.directory(), id);
    const qint64 databaseBytes = QFileInfo(databasePath).size();
    const qint64 sidecarBytes = QFileInfo(dict::keyFilterPathFor(databasePath)).size();
    std::printf("%s: %lld records, %lld keys, maxKeyLength %d, %lld ms, %.1f MB db + %.1f MB keys\n",
                qPrintable(name),
                static_cast<long long>(imported->recordCount),
                static_cast<long long>(imported->keyCount),
                imported->maxKeyLength,
                static_cast<long long>(elapsed),
                static_cast<double>(databaseBytes) / 1048576.0,
                static_cast<double>(sidecarBytes) / 1048576.0);
    return true;
}

const char *modelStateName(ocr::ModelState state)
{
    switch (state) {
    case ocr::ModelState::Missing:
        return "Missing";
    case ocr::ModelState::SizeMismatch:
        return "SizeMismatch";
    case ocr::ModelState::Present:
        return "Present";
    case ocr::ModelState::Verified:
        return "Verified";
    case ocr::ModelState::ChecksumMismatch:
        return "ChecksumMismatch";
    }
    return "?";
}

void printModelStatus()
{
    const QString directory = paths::modelsDir();
    std::printf("models in %s\n", qPrintable(directory));
    const QList<ocr::ModelStatus> statuses = ocr::ModelStore::statusIn(directory, true);
    for (const ocr::ModelStatus &status : statuses) {
        std::printf("  %-40s %s\n", qPrintable(status.info.fileName), modelStateName(status.state));
    }
    std::printf("  required models present: %s\n", ocr::ModelStore::requiredModelsPresentIn(directory) ? "yes" : "no");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("marunine.github.io"));
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("marupop"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Imports dictionaries into the MaruPop dictionary directory without the manager dialog."));
    parser.addHelpOption();
    const QCommandLineOption seedOption{QStringLiteral("seed"),
                                        QStringLiteral("Add the built-in JMdict, JMnedict, KANJIDIC2 and kanji "
                                                       "component entries that the list does not already carry.")};
    const QCommandLineOption listOption{QStringLiteral("list"), QStringLiteral("Print the dictionary list and exit.")};
    const QCommandLineOption modelsOption{QStringLiteral("models"),
                                          QStringLiteral("Print the state of the meikiocr models, verifying each "
                                                         "digest, and exit.")};
    // Declared so --help lists it and so the parser does not reject it; the two tokens that
    // follow it are read off the raw argument list by setSourcePairs(), because
    // QCommandLineParser consumes exactly one value per option occurrence.
    const QCommandLineOption sourceOption{QStringLiteral("set-source"),
                                          QStringLiteral("Point the built-in dictionary <name> at the local file "
                                                         "<path>, so it can be imported without downloading it. "
                                                         "Takes two arguments."),
                                          QStringLiteral("name")};
    const QCommandLineOption yomitanOption{QStringLiteral("add-yomitan"),
                                           QStringLiteral("Add a Yomitan dictionary from a folder or .zip, taking "
                                                          "its name and revision from index.json."),
                                           QStringLiteral("path")};
    const QCommandLineOption asOption{QStringLiteral("as"),
                                      QStringLiteral("The type the preceding --add-yomitan is imported as: word, "
                                                     "kanji, kanjiwordschema, name, pitch, other, frequency or "
                                                     "kanjifrequency. Without it the first detected type is used."),
                                      QStringLiteral("type")};
    const QCommandLineOption enableOption{QStringLiteral("enable"),
                                          QStringLiteral("Enable the dictionary named by display name or id. The "
                                                         "kanji component list is seeded disabled, so a headless "
                                                         "setup that imported it has to turn it on."),
                                          QStringLiteral("name-or-id")};
    const QCommandLineOption disableOption{QStringLiteral("disable"),
                                           QStringLiteral("Disable the dictionary named by display name or id."),
                                           QStringLiteral("name-or-id")};
    const QCommandLineOption importOption{QStringLiteral("import"),
                                          QStringLiteral("Run the import job for the dictionary named by display "
                                                         "name or id, to completion."),
                                          QStringLiteral("name-or-id")};
    parser.addOptions({seedOption,
                       listOption,
                       modelsOption,
                       sourceOption,
                       yomitanOption,
                       asOption,
                       enableOption,
                       disableOption,
                       importOption});
    // The path half of every --set-source pair reaches the parser as a positional argument.
    parser.addPositionalArgument(
        QStringLiteral("path"), QStringLiteral("The path half of a --set-source pair."), QStringLiteral("[path…]"));
    parser.process(app);

    if (parser.isSet(modelsOption)) {
        printModelStatus();
        return 0;
    }

    QString directory = qEnvironmentVariable("MARUPOP_DICT_HOME");
    if (directory.isEmpty())
        directory = paths::dictionariesDir();
    QDir().mkpath(directory);
    std::printf("dictionary directory: %s\n", qPrintable(directory));

    dict::DictionaryManager manager(directory, nullptr);
    if (!manager.load()) {
        std::fprintf(stderr, "the existing dictionaries.json could not be read\n");
        return 1;
    }
    if (parser.isSet(seedOption))
        manager.seedBuiltIns();

    const QList<std::pair<QString, QString>> sources = setSourcePairs(QCoreApplication::arguments());
    for (const auto &[name, rawPath] : sources) {
        dict::Dictionary *entry = resolve(manager, name);
        if (entry == nullptr) {
            std::fprintf(stderr, "no dictionary named %s\n", qPrintable(name));
            return 1;
        }
        const QString path = QFileInfo(rawPath).absoluteFilePath();
        if (!QFileInfo::exists(path)) {
            std::fprintf(stderr, "no such source: %s\n", qPrintable(path));
            return 1;
        }
        (void)manager.setSourcePath(entry->id, path);
        std::printf("%s: source %s\n", qPrintable(entry->name), qPrintable(path));
    }

    const QStringList yomitanSources = parser.values(yomitanOption);
    const QStringList yomitanTypes = parser.values(asOption);
    for (qsizetype i = 0; i < yomitanSources.size(); ++i) {
        const QString source = QFileInfo(yomitanSources.at(i)).absoluteFilePath();
        if (!QFileInfo::exists(source)) {
            std::fprintf(stderr, "no such source: %s\n", qPrintable(source));
            return 1;
        }
        const QList<dict::DictType> detected = dict::YomitanImporter::detectTypes(source);
        if (detected.isEmpty()) {
            std::fprintf(stderr, "no Yomitan bank files in %s\n", qPrintable(source));
            return 1;
        }
        dict::DictType type = detected.constFirst();
        if (i < yomitanTypes.size() && !parseType(yomitanTypes.at(i), &type)) {
            std::fprintf(stderr, "unknown --as type: %s\n", qPrintable(yomitanTypes.at(i)));
            return 1;
        }
        const dict::YomitanIndex index = dict::YomitanImporter::readIndex(source);

        dict::Dictionary entry;
        entry.type = type;
        entry.name = index.valid && !index.title.isEmpty() ? index.title : QFileInfo(source).completeBaseName();
        entry.sourcePath = source;
        entry.revision = index.revision;
        entry.autoUpdatable = index.isUpdatable;
        if (dict::Dictionary *existing = manager.dictionaryNamed(entry.name)) {
            (void)manager.setSourcePath(existing->id, source);
            std::printf("%s: already listed as %s, source updated\n",
                        qPrintable(entry.name),
                        qPrintable(typeName(existing->type)));
            continue;
        }
        const dict::Dictionary *added = manager.add(entry);
        std::printf("%s: added as %s (%s)\n",
                    qPrintable(added->name),
                    qPrintable(typeName(added->type)),
                    qPrintable(added->id.toString(QUuid::WithoutBraces)));
    }

    for (const auto &[option, enabled] : {std::pair{&enableOption, true}, std::pair{&disableOption, false}}) {
        const QStringList names = parser.values(*option);
        for (const QString &name : names) {
            dict::Dictionary *entry = resolve(manager, name);
            if (entry == nullptr) {
                std::fprintf(stderr, "no dictionary named %s\n", qPrintable(name));
                return 1;
            }
            (void)manager.setEnabled(entry->id, enabled);
            std::printf("%s: %s\n", qPrintable(entry->name), enabled ? "enabled" : "disabled");
        }
    }

    bool ok = true;
    const QStringList imports = parser.values(importOption);
    for (const QString &name : imports) {
        dict::Dictionary *entry = resolve(manager, name);
        if (entry == nullptr) {
            std::fprintf(stderr, "no dictionary named %s\n", qPrintable(name));
            ok = false;
            continue;
        }
        if (!runImport(manager, entry))
            ok = false;
    }

    if (!manager.save()) {
        std::fprintf(stderr, "dictionaries.json could not be written\n");
        return 1;
    }
    if (parser.isSet(listOption) || (imports.isEmpty() && yomitanSources.isEmpty() && sources.isEmpty() &&
                                     !parser.isSet(enableOption) && !parser.isSet(disableOption)))
        printList(manager);
    return ok ? 0 : 1;
}

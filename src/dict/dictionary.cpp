// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "dictionary.h"

#include "dict/store.h"

namespace maru::dict
{

namespace
{

// Reads a boolean, keeping the default when the key is absent, so a configuration written by an
// older build gains a new option's default rather than false.
bool boolValue(const QJsonObject &object, QLatin1StringView key, bool fallback)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

} // namespace

QJsonObject DictOptions::toJson() const
{
    QJsonObject object;
    object.insert(QLatin1String("newlineBetweenDefinitions"), newlineBetweenDefinitions);
    object.insert(QLatin1String("wordClassInfo"), wordClassInfo);
    object.insert(QLatin1String("dialectInfo"), dialectInfo);
    object.insert(QLatin1String("primarySpellingOrthographyInfo"), primarySpellingOrthographyInfo);
    object.insert(QLatin1String("alternativeSpellingOrthographyInfo"), alternativeSpellingOrthographyInfo);
    object.insert(QLatin1String("readingOrthographyInfo"), readingOrthographyInfo);
    object.insert(QLatin1String("fieldInfo"), fieldInfo);
    object.insert(QLatin1String("spellingRestrictionInfo"), spellingRestrictionInfo);
    object.insert(QLatin1String("extraDefinitionInfo"), extraDefinitionInfo);
    object.insert(QLatin1String("miscInfo"), miscInfo);
    object.insert(QLatin1String("loanwordEtymology"), loanwordEtymology);
    object.insert(QLatin1String("crossReferences"), crossReferences);
    object.insert(QLatin1String("showImages"), showImages);
    object.insert(QLatin1String("excludeFromAll"), excludeFromAll);
    object.insert(QLatin1String("higherValueMeansHigherFrequency"), higherValueMeansHigherFrequency);
    object.insert(QLatin1String("properNameEntries"), properNameEntries);
    object.insert(QLatin1String("autoUpdateAfterDays"), autoUpdateAfterDays);
    return object;
}

DictOptions DictOptions::fromJson(const QJsonObject &object)
{
    DictOptions options;
    options.newlineBetweenDefinitions =
        boolValue(object, QLatin1StringView("newlineBetweenDefinitions"), options.newlineBetweenDefinitions);
    options.wordClassInfo = boolValue(object, QLatin1StringView("wordClassInfo"), options.wordClassInfo);
    options.dialectInfo = boolValue(object, QLatin1StringView("dialectInfo"), options.dialectInfo);
    options.primarySpellingOrthographyInfo =
        boolValue(object, QLatin1StringView("primarySpellingOrthographyInfo"), options.primarySpellingOrthographyInfo);
    options.alternativeSpellingOrthographyInfo = boolValue(
        object, QLatin1StringView("alternativeSpellingOrthographyInfo"), options.alternativeSpellingOrthographyInfo);
    options.readingOrthographyInfo =
        boolValue(object, QLatin1StringView("readingOrthographyInfo"), options.readingOrthographyInfo);
    options.fieldInfo = boolValue(object, QLatin1StringView("fieldInfo"), options.fieldInfo);
    options.spellingRestrictionInfo =
        boolValue(object, QLatin1StringView("spellingRestrictionInfo"), options.spellingRestrictionInfo);
    options.extraDefinitionInfo =
        boolValue(object, QLatin1StringView("extraDefinitionInfo"), options.extraDefinitionInfo);
    options.miscInfo = boolValue(object, QLatin1StringView("miscInfo"), options.miscInfo);
    options.loanwordEtymology = boolValue(object, QLatin1StringView("loanwordEtymology"), options.loanwordEtymology);
    options.crossReferences = boolValue(object, QLatin1StringView("crossReferences"), options.crossReferences);
    options.showImages = boolValue(object, QLatin1StringView("showImages"), options.showImages);
    options.excludeFromAll = boolValue(object, QLatin1StringView("excludeFromAll"), options.excludeFromAll);
    options.higherValueMeansHigherFrequency = boolValue(
        object, QLatin1StringView("higherValueMeansHigherFrequency"), options.higherValueMeansHigherFrequency);
    options.properNameEntries = boolValue(object, QLatin1StringView("properNameEntries"), options.properNameEntries);
    const QJsonValue days = object.value(QLatin1String("autoUpdateAfterDays"));
    if (days.isDouble())
        options.autoUpdateAfterDays = days.toInt();
    return options;
}

bool optionsRequireReimport(const DictOptions &before, const DictOptions &after)
{
    // properNameEntries is the only kept option that changes which records are written. Every
    // other option is read at render time.
    return before.properNameEntries != after.properNameEntries;
}

bool isWordDictionaryType(DictType type)
{
    switch (type) {
    case DictType::JMdict:
    case DictType::YomitanWord:
    case DictType::CustomWord:
        return true;
    default:
        return false;
    }
}

bool isNameDictionaryType(DictType type)
{
    switch (type) {
    case DictType::JMnedict:
    case DictType::YomitanName:
    case DictType::CustomName:
        return true;
    default:
        return false;
    }
}

bool isKanjiDictionaryType(DictType type)
{
    switch (type) {
    case DictType::Kanjidic:
    case DictType::YomitanKanji:
    case DictType::YomitanKanjiWordSchema:
        return true;
    default:
        return false;
    }
}

bool isFrequencyType(DictType type)
{
    return type == DictType::YomitanFrequency || type == DictType::YomitanKanjiFrequency;
}

bool isPitchAccentType(DictType type)
{
    return type == DictType::YomitanPitchAccent;
}

bool answersCategory(DictType type, LookupCategory category)
{
    // A frequency list and a pitch dictionary decorate the results of the others rather than
    // producing results of their own, and the component list is read by the kanji card alone.
    if (isFrequencyType(type) || isPitchAccentType(type) || type == DictType::KanjiComponents)
        return false;

    switch (category) {
    case LookupCategory::All:
        return true;
    case LookupCategory::Word:
        return isWordDictionaryType(type) || type == DictType::YomitanOther;
    case LookupCategory::Name:
        return isNameDictionaryType(type);
    case LookupCategory::Kanji:
        return isKanjiDictionaryType(type);
    }
    return false;
}

QString dictTypeName(DictType type)
{
    switch (type) {
    case DictType::JMdict:
        return QStringLiteral("JMdict");
    case DictType::JMnedict:
        return QStringLiteral("JMnedict");
    case DictType::Kanjidic:
        return QStringLiteral("KANJIDIC2");
    case DictType::YomitanWord:
        return QStringLiteral("Word dictionary (Yomitan)");
    case DictType::YomitanKanji:
        return QStringLiteral("Kanji dictionary (Yomitan)");
    case DictType::YomitanKanjiWordSchema:
        return QStringLiteral("Kanji dictionary in term-bank format (Yomitan)");
    case DictType::YomitanName:
        return QStringLiteral("Name dictionary (Yomitan)");
    case DictType::YomitanPitchAccent:
        return QStringLiteral("Pitch accent (Yomitan)");
    case DictType::YomitanOther:
        return QStringLiteral("Other (Yomitan)");
    case DictType::YomitanFrequency:
        return QStringLiteral("Frequency list (Yomitan)");
    case DictType::YomitanKanjiFrequency:
        return QStringLiteral("Kanji frequency list (Yomitan)");
    case DictType::CustomWord:
        return QStringLiteral("Custom word list");
    case DictType::CustomName:
        return QStringLiteral("Custom name list");
    case DictType::KanjiComponents:
        return QStringLiteral("Kanji components");
    }
    return QStringLiteral("Unknown");
}

std::vector<std::shared_ptr<const Record>> DictionaryHandle::find(QStringView normalizedKey) const
{
    const std::shared_ptr<Store> held = store;
    if (!held)
        return {};
    return held->find(normalizedKey);
}

const DictionaryHandle *handleFor(const DictionarySnapshot &snapshot, const QUuid &id)
{
    if (!snapshot)
        return nullptr;
    for (const DictionaryHandle &handle : *snapshot) {
        if (handle.id == id)
            return &handle;
    }
    return nullptr;
}

std::vector<std::shared_ptr<const Record>> Dictionary::find(QStringView normalizedKey) const
{
    const std::shared_ptr<Store> held = store;
    if (!held)
        return {};
    return held->find(normalizedKey);
}

bool Dictionary::isReady() const
{
    return enabled && !needsReimport && store && store->isOpen();
}

QJsonObject Dictionary::toJson() const
{
    QJsonObject object;
    object.insert(QLatin1String("id"), id.toString(QUuid::WithoutBraces));
    object.insert(QLatin1String("type"), static_cast<int>(type));
    object.insert(QLatin1String("name"), name);
    object.insert(QLatin1String("sourcePath"), sourcePath);
    object.insert(QLatin1String("updateUrl"), updateUrl.toString());
    object.insert(QLatin1String("revision"), revision);
    object.insert(QLatin1String("enabled"), enabled);
    object.insert(QLatin1String("priority"), priority);
    object.insert(QLatin1String("autoUpdatable"), autoUpdatable);
    object.insert(QLatin1String("builtIn"), builtIn);
    object.insert(QLatin1String("recordCount"), recordCount);
    object.insert(QLatin1String("keyCount"), keyCount);
    object.insert(QLatin1String("maxKeyLength"), maxKeyLength);
    object.insert(QLatin1String("importedAt"), importedAt.isValid() ? importedAt.toString(Qt::ISODate) : QString());
    object.insert(QLatin1String("sourceModifiedAt"),
                  sourceModifiedAt.isValid() ? sourceModifiedAt.toString(Qt::ISODate) : QString());
    object.insert(QLatin1String("options"), options.toJson());
    return object;
}

Dictionary Dictionary::fromJson(const QJsonObject &object)
{
    Dictionary dictionary;
    dictionary.id = QUuid::fromString(object.value(QLatin1String("id")).toString());
    if (dictionary.id.isNull())
        dictionary.id = QUuid::createUuid();
    dictionary.type = static_cast<DictType>(object.value(QLatin1String("type")).toInt());
    dictionary.name = object.value(QLatin1String("name")).toString();
    dictionary.sourcePath = object.value(QLatin1String("sourcePath")).toString();
    dictionary.updateUrl = QUrl(object.value(QLatin1String("updateUrl")).toString());
    dictionary.revision = object.value(QLatin1String("revision")).toString();
    dictionary.enabled = boolValue(object, QLatin1StringView("enabled"), true);
    dictionary.priority = object.value(QLatin1String("priority")).toInt();
    dictionary.autoUpdatable = boolValue(object, QLatin1StringView("autoUpdatable"), false);
    dictionary.builtIn = boolValue(object, QLatin1StringView("builtIn"), false);
    dictionary.recordCount = static_cast<qint64>(object.value(QLatin1String("recordCount")).toDouble());
    dictionary.keyCount = static_cast<qint64>(object.value(QLatin1String("keyCount")).toDouble());
    dictionary.maxKeyLength = object.value(QLatin1String("maxKeyLength")).toInt();
    dictionary.importedAt = QDateTime::fromString(object.value(QLatin1String("importedAt")).toString(), Qt::ISODate);
    dictionary.sourceModifiedAt =
        QDateTime::fromString(object.value(QLatin1String("sourceModifiedAt")).toString(), Qt::ISODate);
    dictionary.options = DictOptions::fromJson(object.value(QLatin1String("options")).toObject());
    return dictionary;
}

QString databasePathFor(const QString &directory, const QUuid &id)
{
    return directory + QLatin1Char('/') + id.toString(QUuid::WithoutBraces) + QLatin1String(".db");
}

QString wordClassTablePathFor(const QString &directory, const QUuid &id)
{
    return directory + QLatin1Char('/') + id.toString(QUuid::WithoutBraces) + QLatin1String(".pos");
}

QString sourceDirectoryFor(const QString &directory, const QUuid &id)
{
    return directory + QLatin1Char('/') + id.toString(QUuid::WithoutBraces);
}

} // namespace maru::dict

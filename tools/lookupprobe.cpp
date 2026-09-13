// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Times maru::lookup::Engine over a dictionary directory the user actually has, which is what a
// latency claim about the hover path needs: the engine's cost scales with the number of enabled
// dictionaries and with the length of the span under the pointer, and neither is visible in a
// hermetic fixture.
//
// It reports five things per run. The check that the answer at the requested maxResults is the
// answer with the undecorated cut out of the pipeline, which is what says the cut inside
// Engine::lookupUncached() preserves the order over a dictionary set no hermetic fixture reaches.
// The whole-call distribution, cold and warm, which is the number the scan loop budgets against.
// The stage split of one cold call, assembled from the same public calls
// Engine::lookupUncached() makes, so a hotspot can be named rather than guessed. The
// per-dictionary split of the query stage, which is what says whether one dictionary or the count
// of them dominates. And the scaling curve over the first k dictionaries of the set, which
// separates a per-dictionary cost from a fixed one.
//
// The stage section times three orderings of the decoration, the sort and the maxResults cut: the
// baseline that decorates every result found; the one that
// memoizes the frequency probe per headword; and the one the engine uses, which cuts on criteria
// 1 to 7 first and attaches the pitch positions to the survivors alone.
//
// Build: cmake -B build -DMARUPOP_BUILD_DEV_TOOLS=ON && cmake --build build --target
//        marupop-lookupprobe
// Run:   MARUPOP_DICT_HOME=~/.local/share/marupop/dictionaries ./build/bin/marupop-lookupprobe
//        ./build/bin/marupop-lookupprobe --dir DIR --max-length 41 --stages --scale
#include "core/paths.h"
#include "deconj/deconjugator.h"
#include "dict/dictionary.h"
#include "dict/dictionarymanager.h"
#include "jp/japanese.h"
#include "lookup/decorate.h"
#include "lookup/engine.h"
#include "lookup/popupadapter.h"
#include "lookup/query.h"
#include "lookup/ranking.h"
#include "lookup/textinfo.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <KLocalizedString>

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <utility>
#include <vector>

using namespace Qt::Literals::StringLiterals;
using namespace maru;

namespace
{

// The hover corpus. Four paragraphs of ordinary prose, because the cost of a lookup is set by
// the span the cursor stands at the head of: conjugated verbs, long katakana runs, proper names
// and kanji compounds each exercise a different part of the candidate generator.
const QStringList &corpus()
{
    static const QStringList paragraphs = {
        u"吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。"_s,
        u"彼女は毎朝六時に起きて、コーヒーを淹れながら新聞を読んでいた。そのあと自転車に乗って駅まで行き、満員電車に揺られて会社へ向かうのが日課だった。"_s,
        u"京都の東山にある清水寺は、平安時代の初めに建てられたと言われている。舞台から見下ろす景色は四季それぞれに美しく、多くの観光客を集めている。"_s,
        u"データベースのインデックスを最適化しなければ、検索の応答時間は利用者が待てる範囲を超えてしまうだろう。実装の詳細については後ほど説明します。"_s,
    };
    return paragraphs;
}

// Every cursor position of the corpus that starts a lookup at all, capped at limit. A position
// on a low surrogate or on a character no dictionary can begin a word with costs nothing and
// would dilute the distribution the popup latency is read off.
struct Position
{
    const QString *text;
    qsizetype index;
};

std::vector<Position> hoverPositions(const QStringList &paragraphs, int limit)
{
    std::vector<Position> positions;
    for (const QString &paragraph : paragraphs) {
        for (qsizetype i = 0; i < paragraph.size(); ++i) {
            if (paragraph.at(i).isLowSurrogate() || paragraph.at(i).isSpace())
                continue;
            positions.push_back({.text = &paragraph, .index = i});
            if (limit > 0 && std::cmp_greater_equal(positions.size(), limit))
                return positions;
        }
    }
    return positions;
}

struct Distribution
{
    double p50 = 0;
    double p90 = 0;
    double p99 = 0;
    double max = 0;
    double mean = 0;
};

Distribution distributionOf(std::vector<double> samples)
{
    Distribution summary;
    if (samples.empty())
        return summary;
    std::ranges::sort(samples);
    const auto at = [&samples](double quantile) {
        const auto index = static_cast<std::size_t>(quantile * static_cast<double>(samples.size() - 1));
        return samples[index];
    };
    summary.p50 = at(0.50);
    summary.p90 = at(0.90);
    summary.p99 = at(0.99);
    summary.max = samples.back();
    summary.mean = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    return summary;
}

void printDistribution(const char *label, const Distribution &summary)
{
    std::printf("  %-28s p50 %8.3f  p90 %8.3f  p99 %8.3f  max %8.3f  mean %8.3f\n",
                label,
                summary.p50,
                summary.p90,
                summary.p99,
                summary.max,
                summary.mean);
}

const char *roleName(dict::DictType type)
{
    if (dict::isFrequencyType(type))
        return "frequency";
    if (dict::isPitchAccentType(type))
        return "pitch";
    if (dict::isWordDictionaryType(type))
        return "word";
    if (dict::isNameDictionaryType(type))
        return "name";
    if (dict::isKanjiDictionaryType(type))
        return "kanji";
    return "other";
}

// The span Engine::lookup() would look up for one position, reproduced here so the stage split
// runs over the same string the whole-call measurement did. Empty where the engine would return
// an empty Response, which is a cursor on a character no dictionary can start a word with, a span
// ended at the cursor by a sentence terminator, and a span opening on whitespace.
QStringView spanFor(const Position &position, int maxSearchLength, int maxKeyLength)
{
    const QStringView text{*position.text};
    qsizetype cursor = position.index;
    if (cursor > 0 && text.at(cursor).isLowSurrogate())
        --cursor;
    int limit = maxSearchLength > 0 ? maxSearchLength : static_cast<int>(text.size());
    if (maxKeyLength > 0)
        limit = std::min(limit, maxKeyLength);
    qsizetype end = text.size();
    if (text.size() - cursor > limit) {
        end = cursor + limit;
        if (end > cursor && text.at(end - 1).isHighSurrogate())
            --end;
    }
    const qsizetype characterLength = text.at(cursor).isHighSurrogate() && cursor + 1 < text.size() ? 2 : 1;
    if (!jp::containsJapaneseCharacters(text.sliced(cursor, characterLength)))
        return {};

    const QStringView searchSpan = text.left(end);
    const qsizetype boundary = jp::findExpressionBoundary(searchSpan, cursor);
    const QStringView span = searchSpan.sliced(cursor, boundary - cursor);
    if (span.isEmpty() || span.at(0).isSpace())
        return {};
    return span;
}

// One line of tools/gen-variant-eval-corpus.py's output: meikiocr's corrected read of a rendered
// line, the text that was rendered, and the confidence of each read character. The two strings
// have one length, so an index into one is the same position in the other.
struct PairLine
{
    QString read;
    QString truth;
    QList<float> confidences;
    // Index-parallel to read: true where the read character differs from the rendered one after
    // NFKC, so equivalent Unicode spellings compare equally.
    QList<bool> misread;
};

std::vector<PairLine> readPairs(const QString &path)
{
    std::vector<PairLine> lines;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return lines;
    while (!file.atEnd()) {
        const QJsonObject object = QJsonDocument::fromJson(file.readLine()).object();
        PairLine line;
        line.read = object.value("read"_L1).toString();
        line.truth = object.value("truth"_L1).toString();
        if (line.read.isEmpty() || line.read.size() != line.truth.size())
            continue;
        for (const QJsonValue value : object.value("conf"_L1).toArray())
            line.confidences.append(static_cast<float>(value.toDouble()));
        for (qsizetype i = 0; i < line.read.size(); ++i) {
            const QString left = QString(line.read.at(i)).normalized(QString::NormalizationForm_KC);
            const QString right = QString(line.truth.at(i)).normalized(QString::NormalizationForm_KC);
            line.misread.append(left != right);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

// Whether two first results show one headword over one span length. The truth and the read have
// one length, so a result on each is the same answer where this holds. The dictionary and the
// reading list are left out: two dictionaries that both hold ダガー answer the same word, and a
// comparison that counted them apart would score the pass by which dictionary ranked first.
bool sameAnswer(const lookup::Result &left, const lookup::Result &right)
{
    return left.primarySpelling == right.primarySpelling && left.matchedText.size() == right.matchedText.size();
}

// The --pairs evaluation. At every position of every line it compares three first results: the
// rendered text with the pass off, which is the answer a perfect read would get; the read with the
// pass off; and the read with the pass on. A position whose read answer already is the rendered
// one is intact, and the pass regresses it by answering anything else. A position whose read
// answer is not, where a misread character lies inside the rendered answer's span, is damaged, and
// the pass recovers it by answering the rendered one.
int evaluatePairs(lookup::Engine &engine,
                  const std::vector<PairLine> &lines,
                  const lookup::VariantOptions &variants,
                  bool passConfidences,
                  int cleanEvery,
                  int maxSearchLength,
                  int maxResults)
{
    lookup::VariantOptions off = variants;
    off.enabled = false;
    const auto all = [&](const QString &text,
                         qsizetype index,
                         const lookup::VariantOptions &options,
                         const QList<float> &confidences) {
        engine.setVariantOptions(options);
        lookup::Request request;
        request.sourceText = text;
        request.cursorIndex = index;
        request.maxSearchLength = maxSearchLength;
        request.maxResults = maxResults;
        request.confidences = confidences;
        return engine.lookup(request).results;
    };
    const auto first = [&](const QString &text,
                           qsizetype index,
                           const lookup::VariantOptions &options,
                           const QList<float> &confidences) -> std::optional<lookup::Result> {
        const QList<lookup::Result> results = all(text, index, options, confidences);
        if (results.isEmpty())
            return std::nullopt;
        return results.first();
    };

    qsizetype linesUsed = 0;
    qsizetype misreadLines = 0;
    qsizetype positions = 0;
    qsizetype intact = 0;
    qsizetype regressed = 0;
    qsizetype regressedClean = 0;
    qsizetype cleanPositions = 0;
    qsizetype damaged = 0;
    qsizetype recovered = 0;
    // Damaged positions whose rendered answer is anywhere in the list the pass answers, which is
    // what VariantOptions::rankFirst off is measured by.
    qsizetype listed = 0;
    qsizetype listedBefore = 0;
    qsizetype changedWrong = 0;
    qsizetype damagedOutside = 0;
    qsizetype recoveredOutside = 0;
    qsizetype variantFirst = 0;
    QStringList regressions;
    QStringList recoveries;
    std::vector<double> cold;
    for (std::size_t n = 0; n < lines.size(); ++n) {
        const PairLine &line = lines[n];
        const bool clean = !line.misread.contains(true);
        if (clean && (cleanEvery <= 0 || n % static_cast<std::size_t>(cleanEvery) != 0))
            continue;
        ++linesUsed;
        if (!clean)
            ++misreadLines;
        const QList<float> confidences = passConfidences ? line.confidences : QList<float>{};
        for (qsizetype i = 0; i < line.read.size(); ++i) {
            if (line.read.at(i).isSpace() || line.read.at(i).isLowSurrogate())
                continue;
            const std::optional<lookup::Result> truth = first(line.truth, i, off, {});
            if (!truth.has_value())
                continue;
            ++positions;
            const QList<lookup::Result> exactList = all(line.read, i, off, {});
            const std::optional<lookup::Result> exact =
                exactList.isEmpty() ? std::nullopt : std::optional<lookup::Result>(exactList.first());
            engine.invalidateCache();
            QElapsedTimer timer;
            timer.start();
            const QList<lookup::Result> variedList = all(line.read, i, variants, confidences);
            cold.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
            const std::optional<lookup::Result> varied =
                variedList.isEmpty() ? std::nullopt : std::optional<lookup::Result>(variedList.first());
            const auto contains = [&truth](const QList<lookup::Result> &list) {
                return std::ranges::any_of(list, [&truth](const lookup::Result &result) {
                    return sameAnswer(result, *truth);
                });
            };
            if (varied.has_value() && varied->matchedByVariant)
                ++variantFirst;

            const bool exactRight = exact.has_value() && sameAnswer(*exact, *truth);
            const bool variedRight = varied.has_value() && sameAnswer(*varied, *truth);
            const bool variedChanged = varied.has_value() != exact.has_value() ||
                                       (varied.has_value() && exact.has_value() && !sameAnswer(*varied, *exact));
            const QString example = line.read.mid(i, std::max<qsizetype>(truth->matchedText.size(), 1)) + u" → "_s +
                                    (varied.has_value() ? varied->primarySpelling : u"(none)"_s) + u" (rendered "_s +
                                    line.truth.mid(i, truth->matchedText.size()) + u" → "_s + truth->primarySpelling +
                                    u")"_s;

            if (exactRight) {
                ++intact;
                if (clean)
                    ++cleanPositions;
                if (!variedRight) {
                    ++regressed;
                    if (clean)
                        ++regressedClean;
                    if (regressions.size() < 40)
                        regressions.append(example);
                }
                continue;
            }
            bool inWindow = false;
            for (qsizetype j = i; j < i + truth->matchedText.size() && j < line.misread.size(); ++j)
                inWindow = inWindow || line.misread.at(j);
            if (inWindow) {
                ++damaged;
                if (contains(exactList))
                    ++listedBefore;
                if (contains(variedList))
                    ++listed;
                if (variedRight) {
                    ++recovered;
                    if (recoveries.size() < 40)
                        recoveries.append(example);
                } else if (variedChanged) {
                    ++changedWrong;
                }
            } else {
                ++damagedOutside;
                if (variedRight)
                    ++recoveredOutside;
            }
        }
    }

    const auto share = [](qsizetype part, qsizetype whole) {
        return whole > 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
    };
    std::printf("pairs: %lld lines (%lld with a misread), %lld positions with a rendered answer\n",
                static_cast<long long>(linesUsed),
                static_cast<long long>(misreadLines),
                static_cast<long long>(positions));
    std::printf("  intact    %7lld, regressed %6lld (%.2f %%); on clean lines %lld of %lld (%.2f %%)\n",
                static_cast<long long>(intact),
                static_cast<long long>(regressed),
                share(regressed, intact),
                static_cast<long long>(regressedClean),
                static_cast<long long>(cleanPositions),
                share(regressedClean, cleanPositions));
    std::printf("  damaged   %7lld, recovered %6lld (%.2f %%), changed to another wrong answer %lld\n",
                static_cast<long long>(damaged),
                static_cast<long long>(recovered),
                share(recovered, damaged),
                static_cast<long long>(changedWrong));
    std::printf("  damaged whose rendered answer is listed: %lld with the pass, %lld without (top %d)\n",
                static_cast<long long>(listed),
                static_cast<long long>(listedBefore),
                maxResults);
    std::printf("  damaged outside the answer's span %lld, recovered %lld\n",
                static_cast<long long>(damagedOutside),
                static_cast<long long>(recoveredOutside));
    std::printf("  first result from the variant pass at %lld positions\n", static_cast<long long>(variantFirst));
    printDistribution("cold, pass as configured", distributionOf(cold));
    std::printf("  recoveries:\n");
    for (const QString &example : std::as_const(recoveries))
        std::printf("    %s\n", qPrintable(example));
    std::printf("  regressions:\n");
    for (const QString &example : std::as_const(regressions))
        std::printf("    %s\n", qPrintable(example));
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("marupop"));
    QCoreApplication::setApplicationName(u"marupop-lookupprobe"_s);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Times maru::lookup::Engine over a real dictionary set."_s);
    parser.addHelpOption();
    const QCommandLineOption directoryOption(u"dir"_s, u"The dictionary directory."_s, u"path"_s);
    const QCommandLineOption positionsOption(u"positions"_s, u"Hover positions to time (default all)."_s, u"n"_s);
    const QCommandLineOption lengthOption(u"max-length"_s, u"Request::maxSearchLength (default 41)."_s, u"n"_s);
    const QCommandLineOption resultsOption(u"max-results"_s, u"Request::maxResults (default 10)."_s, u"n"_s);
    const QCommandLineOption stagesOption(u"stages"_s, u"Report the stage and per-dictionary split."_s);
    const QCommandLineOption scaleOption(u"scale"_s, u"Report the curve over the first k dictionaries."_s);
    const QCommandLineOption textOption(u"text"_s, u"A UTF-8 file of paragraphs, one per line."_s, u"path"_s);
    const QCommandLineOption variantsOption(u"ocr-variants"_s, u"Enable the recognition-variant pass."_s);
    const QCommandLineOption shareOption(
        u"substitutable-share"_s,
        u"Pass confidences marking this share of characters substitutable (default: none passed)."_s,
        u"x"_s);
    const QCommandLineOption gateOption(u"gate"_s, u"VariantOptions::confidenceGate (default 0.8)."_s, u"x"_s);
    const QCommandLineOption keyLengthOption(
        u"variant-key-length"_s, u"VariantOptions::maxKeyLength, 0 for none (default 12)."_s, u"n"_s);
    const QCommandLineOption keysOption(
        u"variant-keys"_s, u"VariantOptions::maxKeys, 0 for none (default 400)."_s, u"n"_s);
    const QCommandLineOption anyWordOption(u"any-word"_s, u"VariantAcceptance::AnyWord."_s);
    const QCommandLineOption shortOption(u"short-hiragana"_s, u"VariantOptions::shortAndHiraganaMatches."_s);
    const QCommandLineOption namesOption(u"variant-names"_s, u"VariantOptions::nameDictionaries."_s);
    const QCommandLineOption afterOption(u"variants-after"_s, u"VariantOptions::rankFirst off."_s);
    const QCommandLineOption baselineOption(
        u"baseline"_s, u"With --pairs, time the read with the pass off, for the cost the pass adds."_s);
    const QCommandLineOption excludeOption(
        u"exclude"_s, u"Leave the dictionary of this name out of the set; repeatable."_s, u"name"_s);
    const QCommandLineOption pairsOption(
        u"pairs"_s, u"Evaluate the pass over a tools/gen-variant-eval-corpus.py file."_s, u"path"_s);
    const QCommandLineOption cleanEveryOption(
        u"clean-every"_s, u"With --pairs, use every nth line that holds no misread (default 10)."_s, u"n"_s);
    const QCommandLineOption noConfidencesOption(u"no-confidences"_s,
                                                 u"With --pairs, pass no confidences, as a Screen AI read does."_s);
    parser.addOptions({directoryOption,    positionsOption, lengthOption,   resultsOption, stagesOption,
                       scaleOption,        textOption,      variantsOption, shareOption,   gateOption,
                       keyLengthOption,    keysOption,      anyWordOption,  shortOption,   namesOption,
                       afterOption,        baselineOption,  excludeOption,  pairsOption,   cleanEveryOption,
                       noConfidencesOption});
    parser.process(app);

    QString directory = parser.value(directoryOption);
    if (directory.isEmpty())
        directory = qEnvironmentVariable("MARUPOP_DICT_HOME");
    if (directory.isEmpty())
        directory = paths::dictionariesDir();

    const int maxSearchLength = parser.isSet(lengthOption) ? parser.value(lengthOption).toInt() : 41;
    const int maxResults = parser.isSet(resultsOption) ? parser.value(resultsOption).toInt() : 10;
    const int positionLimit = parser.isSet(positionsOption) ? parser.value(positionsOption).toInt() : 0;

    QStringList paragraphs = corpus();
    if (parser.isSet(textOption)) {
        QFile file(parser.value(textOption));
        if (!file.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "cannot read %s\n", qPrintable(parser.value(textOption)));
            return 1;
        }
        paragraphs = QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
    }

    dict::DictionaryManager manager(directory, nullptr);
    if (!manager.load()) {
        std::fprintf(stderr, "the dictionaries.json in %s could not be read\n", qPrintable(directory));
        return 1;
    }
    dict::DictionarySnapshot snapshot = manager.snapshot();
    if (parser.isSet(excludeOption)) {
        const QStringList excluded = parser.values(excludeOption);
        auto kept = std::make_shared<std::vector<dict::DictionaryHandle>>();
        for (const dict::DictionaryHandle &handle : *snapshot) {
            if (!excluded.contains(handle.name))
                kept->push_back(handle);
        }
        snapshot = kept;
    }
    std::printf("dictionary directory: %s\n", qPrintable(directory));
    std::printf("enabled dictionaries: %d\n\n", static_cast<int>(snapshot->size()));

    int maxKeyLength = 0;
    qint64 totalRecords = 0;
    std::printf("%-3s %-38s %-10s %10s %8s\n", "#", "name", "role", "keys", "maxKey");
    for (const dict::DictionaryHandle &handle : *snapshot) {
        maxKeyLength = std::max(maxKeyLength, handle.maxKeyLength);
        const dict::Dictionary *entry = manager.dictionary(handle.id);
        if (entry != nullptr)
            totalRecords += entry->recordCount;
        std::printf("%-3d %-38s %-10s %10lld %8d\n",
                    handle.priority,
                    qPrintable(handle.name),
                    roleName(handle.type),
                    static_cast<long long>(entry != nullptr ? entry->keyCount : 0),
                    handle.maxKeyLength);
    }
    std::printf("total records %lld, longest key %d\n\n", static_cast<long long>(totalRecords), maxKeyLength);

    QString rulesError;
    const std::optional<deconj::RuleSet> rules = deconj::RuleSet::loadEmbedded(&rulesError);
    if (!rules.has_value()) {
        std::fprintf(stderr, "the deconjugation rules did not load: %s\n", qPrintable(rulesError));
        return 1;
    }

    const std::vector<Position> positions = hoverPositions(paragraphs, positionLimit);
    std::printf("hover positions: %d, maxSearchLength %d, maxResults %d\n\n",
                static_cast<int>(positions.size()),
                maxSearchLength,
                maxResults);

    lookup::Engine engine(*rules);
    engine.setDictionaries(snapshot, manager.wordClasses());
    lookup::VariantOptions variants;
    variants.enabled = parser.isSet(variantsOption) || (parser.isSet(pairsOption) && !parser.isSet(baselineOption));
    // The probe measures the pass itself, so it runs on a request without confidences too, which
    // is what --no-confidences and a run without --substitutable-share time.
    variants.withoutConfidences = true;
    if (parser.isSet(gateOption))
        variants.confidenceGate = parser.value(gateOption).toFloat();
    if (parser.isSet(keyLengthOption))
        variants.maxKeyLength = parser.value(keyLengthOption).toInt();
    if (parser.isSet(keysOption))
        variants.maxKeys = parser.value(keysOption).toInt();
    if (parser.isSet(anyWordOption))
        variants.acceptance = lookup::VariantAcceptance::AnyWord;
    variants.shortAndHiraganaMatches = parser.isSet(shortOption);
    variants.nameDictionaries = parser.isSet(namesOption);
    variants.rankFirst = !parser.isSet(afterOption);
    engine.setVariantOptions(variants);
    std::printf("recognition-variant pass: %s, gate %.2f, key length %d, keys %d, %s, short and hiragana %s, "
                "names %s, %s\n\n",
                variants.enabled ? "on" : "off",
                static_cast<double>(variants.confidenceGate),
                variants.maxKeyLength,
                variants.maxKeys,
                variants.acceptance == lookup::VariantAcceptance::AnyWord ? "any word" : "common words",
                variants.shortAndHiraganaMatches ? "on" : "off",
                variants.nameDictionaries ? "on" : "off",
                variants.rankFirst ? "variants first" : "variants after exact results");

    if (parser.isSet(pairsOption)) {
        const std::vector<PairLine> lines = readPairs(parser.value(pairsOption));
        if (lines.empty()) {
            std::fprintf(stderr, "no aligned lines in %s\n", qPrintable(parser.value(pairsOption)));
            return 1;
        }
        const int cleanEvery = parser.isSet(cleanEveryOption) ? parser.value(cleanEveryOption).toInt() : 10;
        return evaluatePairs(
            engine, lines, variants, !parser.isSet(noConfidencesOption), cleanEvery, maxSearchLength, maxResults);
    }

    // A seeded draw of characters eligible for substitution at the requested share.
    // This synthetic distribution is a workload control, not a measured OCR distribution.
    const bool simulateConfidences = parser.isSet(shareOption);
    const double substitutableShare = parser.value(shareOption).toDouble();
    const auto confidencesFor = [&](const QString &text) {
        QList<float> confidences;
        if (!simulateConfidences)
            return confidences;
        std::uint32_t state = static_cast<std::uint32_t>(qHash(text)) | 1U;
        confidences.reserve(text.size());
        for (qsizetype i = 0; i < text.size(); ++i) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            const double draw = static_cast<double>(state) / 4294967296.0;
            confidences.append(draw < substitutableShare ? 0.5F : 0.99F);
        }
        return confidences;
    };
    if (simulateConfidences)
        std::printf("simulated substitutable share: %.3f\n\n", substitutableShare);

    const auto timeOnce = [&](const Position &position, bool cold) {
        lookup::Request request;
        request.sourceText = *position.text;
        request.confidences = confidencesFor(*position.text);
        request.cursorIndex = position.index;
        request.maxSearchLength = maxSearchLength;
        request.maxResults = maxResults;
        if (cold)
            engine.invalidateCache();
        QElapsedTimer timer;
        timer.start();
        const lookup::Response response = engine.lookup(request);
        const double elapsed = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
        return std::pair{elapsed, response};
    };

    // The answer Engine::lookup() gives at the requested maxResults against the answer it gives
    // with the cut out of the pipeline, which is what says the undecorated cut inside
    // lookupUncached() preserves the order over a dictionary set no hermetic fixture reaches.
    {
        qsizetype mismatches = 0;
        for (const Position &position : positions) {
            lookup::Request request;
            request.sourceText = *position.text;
            request.cursorIndex = position.index;
            request.maxSearchLength = maxSearchLength;
            request.maxResults = maxResults;
            engine.invalidateCache();
            const lookup::Response cut = engine.lookup(request);
            request.maxResults = 0;
            engine.invalidateCache();
            const lookup::Response whole = engine.lookup(request);

            const qsizetype expected =
                maxResults > 0 ? std::min<qsizetype>(maxResults, whole.results.size()) : whole.results.size();
            if (cut.results.size() != expected) {
                ++mismatches;
                continue;
            }
            for (qsizetype i = 0; i < expected; ++i) {
                const lookup::Result &left = whole.results.at(i);
                const lookup::Result &right = cut.results.at(i);
                if (left.dictionary.id != right.dictionary.id || left.matchedText != right.matchedText ||
                    left.primarySpelling != right.primarySpelling || left.readings != right.readings ||
                    left.pitchPositions != right.pitchPositions ||
                    lookup::frequencyScore(left) != lookup::frequencyScore(right) ||
                    lookup::definitionText(left) != lookup::definitionText(right)) {
                    ++mismatches;
                    break;
                }
            }
        }
        std::printf("Undecorated cut: %lld of %d positions answered a different top %d\n\n",
                    static_cast<long long>(mismatches),
                    static_cast<int>(positions.size()),
                    maxResults);
    }
    engine.invalidateCache();

    // One untimed pass, so the store record caches and the SQLite page cache are in the state a
    // running application's are rather than in the state a fresh process's are.
    for (const Position &position : positions)
        (void)timeOnce(position, true);

    std::vector<double> cold;
    std::vector<double> warm;
    std::vector<double> adapt;
    qsizetype resultCount = 0;
    qsizetype emptyCount = 0;
    // Positions whose first result the recognition-variant pass found. The corpus is correctly
    // spelled text, so each one is a candidate false positive of the pass.
    qsizetype variantFirstCount = 0;
    QStringList variantFirstExamples;
    for (const Position &position : positions) {
        const auto [elapsed, response] = timeOnce(position, true);
        cold.push_back(elapsed);
        resultCount += response.results.size();
        if (response.results.isEmpty())
            ++emptyCount;
        if (!response.results.isEmpty() && response.results.first().matchedByVariant) {
            ++variantFirstCount;
            const lookup::Result &first = response.results.first();
            variantFirstExamples.append(first.matchedText + u" → "_s + first.primarySpelling);
        }

        QElapsedTimer timer;
        timer.start();
        const popup::PopupModel model = lookup::toPopupModel(response);
        adapt.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
        (void)model;
    }
    engine.invalidateCache();
    for (const Position &position : positions)
        (void)timeOnce(position, false);
    warm.reserve(positions.size());
    for (const Position &position : positions)
        warm.push_back(timeOnce(position, false).first);

    // The share of positions past the 5 ms the scan loop budgets per hover, which is the
    // number a percentile alone does not give.
    const double budgetMs = 5.0;
    const auto overBudget = static_cast<qsizetype>(std::ranges::count_if(cold, [budgetMs](double sample) {
        return sample > budgetMs;
    }));

    std::printf("Engine::lookup(), milliseconds\n");
    printDistribution("cold (cache dropped)", distributionOf(cold));
    printDistribution("warm (cache hit)", distributionOf(warm));
    printDistribution("toPopupModel()", distributionOf(adapt));
    // The five slowest positions, so the tail can be read as a property of the span rather than
    // as noise. The span length and the result count are what the cost tracks.
    {
        std::vector<std::pair<double, std::size_t>> ranked;
        ranked.reserve(cold.size());
        for (std::size_t i = 0; i < cold.size(); ++i)
            ranked.emplace_back(cold[i], i);
        std::ranges::sort(ranked, [](const auto &left, const auto &right) {
            return left.first > right.first;
        });
        std::printf("  slowest positions: ");
        for (std::size_t i = 0; i < ranked.size() && i < 5; ++i) {
            const Position &position = positions[ranked[i].second];
            const QStringView span = spanFor(position, maxSearchLength, maxKeyLength);
            std::printf("%.2f ms over %d units (%s)%s",
                        ranked[i].first,
                        static_cast<int>(span.size()),
                        qPrintable(span.left(6).toString()),
                        i + 1 < 5 ? ", " : "\n");
        }
    }
    std::printf("  %lld of %d cold positions cost more than %.1f ms\n",
                static_cast<long long>(overBudget),
                static_cast<int>(positions.size()),
                budgetMs);
    std::printf("  results %lld over %d positions, %lld positions answered nothing\n\n",
                static_cast<long long>(resultCount),
                static_cast<int>(positions.size()),
                static_cast<long long>(emptyCount));
    if (engine.variantOptions().enabled) {
        std::printf("  first result from the variant pass at %lld of %d positions\n",
                    static_cast<long long>(variantFirstCount),
                    static_cast<int>(positions.size()));
        for (const QString &example : std::as_const(variantFirstExamples))
            std::printf("    %s\n", qPrintable(example));
        std::printf("\n");
    }

    if (parser.isSet(stagesOption)) {
        // The stages of Engine::lookupUncached(), driven through the same public calls, so the
        // sum is the cold number above minus the cache bookkeeping.
        std::vector<double> textInfo;
        std::vector<double> words;
        std::vector<double> names;
        std::vector<double> kanji;
        std::vector<double> decorateStage;
        std::vector<double> frequencyHalf;
        std::vector<double> pitchHalf;
        std::vector<double> memoizedHalf;
        std::vector<double> projectedPipeline;
        std::vector<double> prefiltered;
        std::vector<double> prefilteredTotal;
        qsizetype survivorTotal = 0;
        qsizetype orderMismatches = 0;
        std::vector<double> stagedTotal;
        std::vector<double> projectedTotal;
        std::vector<double> sortStage;
        qsizetype distinctHeadwords = 0;
        QList<dict::DictionaryHandle> wordDicts;
        QList<dict::DictionaryHandle> nameDicts;
        QList<dict::DictionaryHandle> kanjiDicts;
        lookup::Decorators decorators;
        for (const dict::DictionaryHandle &handle : *snapshot) {
            if (dict::isFrequencyType(handle.type)) {
                if (handle.type == dict::DictType::YomitanKanjiFrequency)
                    decorators.kanjiFrequencies.append(handle);
                else
                    decorators.wordFrequencies.append(handle);
            } else if (dict::isPitchAccentType(handle.type)) {
                decorators.pitchAccents.append(handle);
            } else if (handle.options.excludeFromAll) {
                continue;
            } else if (dict::isWordDictionaryType(handle.type)) {
                wordDicts.append(handle);
            } else if (dict::isNameDictionaryType(handle.type)) {
                nameDicts.append(handle);
            } else if (dict::isKanjiDictionaryType(handle.type)) {
                kanjiDicts.append(handle);
            }
        }
        std::vector<double> perDictionary(static_cast<std::size_t>(wordDicts.size() + nameDicts.size()), 0.0);
        const std::shared_ptr<const dict::WordClassTable> wordClasses = manager.wordClasses();

        // Every position the stage loop timed, which is fewer than the hover positions: a cursor
        // the engine answers with an empty Response reaches no stage, and dividing by the
        // position count would report every per-lookup figure below low.
        qsizetype timed = 0;
        qsizetype candidateTotal = 0;
        qsizetype lemmaTotal = 0;
        qsizetype preTruncation = 0;
        for (const Position &position : positions) {
            const QStringView span = spanFor(position, maxSearchLength, maxKeyLength);
            if (span.isEmpty())
                continue;

            ++timed;
            QElapsedTimer timer;
            timer.start();
            const lookup::TextInfo info = lookup::buildTextInfo(*rules, span);
            textInfo.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
            candidateTotal += info.candidates.size();
            lemmaTotal += info.deconjugatedTexts.size();

            QList<lookup::Result> results;
            timer.restart();
            std::size_t slot = 0;
            for (const dict::DictionaryHandle &handle : wordDicts) {
                QElapsedTimer one;
                one.start();
                results.append(lookup::queryWordDictionary(handle, info, *wordClasses));
                perDictionary[slot++] += static_cast<double>(one.nsecsElapsed()) / 1.0e6;
            }
            words.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);

            timer.restart();
            for (const dict::DictionaryHandle &handle : nameDicts) {
                QElapsedTimer one;
                one.start();
                results.append(lookup::queryNameDictionary(handle, info));
                perDictionary[slot++] += static_cast<double>(one.nsecsElapsed()) / 1.0e6;
            }
            names.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);

            // The frequency half and the pitch half of decorate(), timed apart: only the first
            // feeds the comparator, so only the first has to run before the maxResults cut.
            lookup::Decorators frequenciesOnly{.wordFrequencies = decorators.wordFrequencies,
                                               .kanjiFrequencies = decorators.kanjiFrequencies,
                                               .pitchAccents = {}};
            lookup::Decorators pitchOnly{
                .wordFrequencies = {}, .kanjiFrequencies = {}, .pitchAccents = decorators.pitchAccents};
            QList<lookup::Result> copy = results;
            timer.restart();
            for (lookup::Result &result : copy)
                lookup::decorate(result, frequenciesOnly);
            frequencyHalf.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
            timer.restart();
            for (lookup::Result &result : copy)
                lookup::decorate(result, pitchOnly);
            pitchHalf.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);

            // The same frequency work with one probe per distinct (primary spelling, readings)
            // rather than one per result, which is what says how much of the stage is the same
            // headword answered by several dictionaries.
            timer.restart();
            QHash<QString, QList<lookup::FrequencyHit>> memo;
            for (lookup::Result &result : results) {
                const QString headword = result.primarySpelling % u"\x1f"_s % result.readings.join(u'\x1e');
                const auto cached = memo.constFind(headword);
                if (cached != memo.constEnd()) {
                    result.frequencies = *cached;
                    if (!decorators.wordFrequencies.isEmpty())
                        result.priorityRank = 0;
                    continue;
                }
                lookup::decorate(result, frequenciesOnly);
                memo.insert(headword, result.frequencies);
            }
            memoizedHalf.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
            distinctHeadwords += memo.size();
            decorateStage.push_back(frequencyHalf.back() + pitchHalf.back());

            timer.restart();
            const std::optional<QString> head = jp::firstCharacterIfKanji(span);
            if (head.has_value()) {
                for (const dict::DictionaryHandle &handle : kanjiDicts)
                    (void)lookup::queryKanjiDictionary(handle, *head);
            }
            kanji.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);

            preTruncation += results.size();
            timer.restart();
            std::ranges::stable_sort(results, lookup::lessThan);
            sortStage.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);

            // The same three stages in the order that pays for the results the popup shows
            // rather than for every result any dictionary produced: memoized frequency, which
            // criterion 8 needs over the whole list, then the sort and the maxResults cut, then
            // the pitch probe over the survivors alone, which nothing but the popup reads.
            QList<lookup::Result> projected = copy;
            for (lookup::Result &result : projected)
                result.frequencies.clear();
            timer.restart();
            QHash<QString, QList<lookup::FrequencyHit>> projectedMemo;
            for (lookup::Result &result : projected) {
                const QString headword = result.primarySpelling % u"\x1f"_s % result.readings.join(u'\x1e');
                const auto cached = projectedMemo.constFind(headword);
                if (cached != projectedMemo.constEnd()) {
                    result.frequencies = *cached;
                    if (!decorators.wordFrequencies.isEmpty())
                        result.priorityRank = 0;
                    continue;
                }
                lookup::decorate(result, frequenciesOnly);
                projectedMemo.insert(headword, result.frequencies);
            }
            std::ranges::stable_sort(projected, lookup::lessThan);
            if (maxResults > 0 && projected.size() > maxResults)
                projected.erase(projected.begin() + maxResults, projected.end());
            for (lookup::Result &result : projected)
                lookup::decorate(result, pitchOnly);
            projectedPipeline.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);

            // The same again with the frequency-independent pre-cut in front of it.
            QList<lookup::Result> prefilter = copy;
            for (lookup::Result &result : prefilter)
                result.frequencies.clear();
            timer.restart();
            if (maxResults > 0 && prefilter.size() > maxResults) {
                std::ranges::stable_sort(prefilter, lookup::lessThanUndecorated);
                qsizetype keep = maxResults;
                while (keep < prefilter.size() &&
                       lookup::compareUndecorated(prefilter.at(keep - 1), prefilter.at(keep)) == 0)
                    ++keep;
                prefilter.erase(prefilter.begin() + keep, prefilter.end());
            }
            QHash<QString, QList<lookup::FrequencyHit>> prefilterMemo;
            for (lookup::Result &result : prefilter) {
                const QString headword = result.primarySpelling % u"\x1f"_s % result.readings.join(u'\x1e');
                const auto cached = prefilterMemo.constFind(headword);
                if (cached != prefilterMemo.constEnd()) {
                    result.frequencies = *cached;
                    if (!decorators.wordFrequencies.isEmpty())
                        result.priorityRank = 0;
                    continue;
                }
                lookup::decorate(result, frequenciesOnly);
                prefilterMemo.insert(headword, result.frequencies);
            }
            std::ranges::stable_sort(prefilter, lookup::lessThan);
            if (maxResults > 0 && prefilter.size() > maxResults)
                prefilter.erase(prefilter.begin() + maxResults, prefilter.end());
            for (lookup::Result &result : prefilter)
                lookup::decorate(result, pitchOnly);
            prefiltered.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
            survivorTotal += prefilter.size();

            // The pre-cut is only worth proposing if it answers what the current order answers.
            // The reference is the fully decorated list sorted and cut the way lookupUncached()
            // does it, compared entry for entry.
            QList<lookup::Result> reference = copy;
            std::ranges::stable_sort(reference, lookup::lessThan);
            if (maxResults > 0 && reference.size() > maxResults)
                reference.erase(reference.begin() + maxResults, reference.end());
            if (reference.size() != prefilter.size()) {
                ++orderMismatches;
            } else {
                for (qsizetype i = 0; i < reference.size(); ++i) {
                    const lookup::Result &left = reference.at(i);
                    const lookup::Result &right = prefilter.at(i);
                    if (left.dictionary.id != right.dictionary.id || left.matchedText != right.matchedText ||
                        left.primarySpelling != right.primarySpelling || left.readings != right.readings ||
                        lookup::definitionText(left) != lookup::definitionText(right)) {
                        ++orderMismatches;
                        break;
                    }
                }
            }

            const double fixed = textInfo.back() + words.back() + names.back() + kanji.back();
            stagedTotal.push_back(fixed + decorateStage.back() + sortStage.back());
            projectedTotal.push_back(fixed + projectedPipeline.back());
            prefilteredTotal.push_back(fixed + prefiltered.back());
        }

        std::printf("Stages of one cold lookup, milliseconds\n");
        printDistribution("buildTextInfo()", distributionOf(textInfo));
        printDistribution("queryWordDictionary() x N", distributionOf(words));
        printDistribution("queryNameDictionary() x N", distributionOf(names));
        printDistribution("queryKanjiDictionary() x N", distributionOf(kanji));
        printDistribution("decorate() per result", distributionOf(decorateStage));
        printDistribution("  of which frequency", distributionOf(frequencyHalf));
        printDistribution("  of which pitch accent", distributionOf(pitchHalf));
        printDistribution("  frequency, memoized", distributionOf(memoizedHalf));
        printDistribution("stable_sort()", distributionOf(sortStage));
        printDistribution("frequency+sort+cut+pitch", distributionOf(projectedPipeline));
        printDistribution("pre-cut+frequency+sort+pitch", distributionOf(prefiltered));
        printDistribution("total, decorate every result", distributionOf(stagedTotal));
        printDistribution("total, memoized frequency", distributionOf(projectedTotal));
        printDistribution("total, the engine's order", distributionOf(prefilteredTotal));
        std::printf("  candidates %.1f per lookup, lemmas %.1f per lookup, results before the "
                    "maxResults cut %.1f, distinct headwords among them %.1f, survivors of the "
                    "frequency-independent pre-cut %.1f, over the %lld of %d positions the engine "
                    "answers at all; the pre-cut answered a different top %d at %lld of them\n\n",
                    static_cast<double>(candidateTotal) / static_cast<double>(timed),
                    static_cast<double>(lemmaTotal) / static_cast<double>(timed),
                    static_cast<double>(preTruncation) / static_cast<double>(timed),
                    static_cast<double>(distinctHeadwords) / static_cast<double>(timed),
                    static_cast<double>(survivorTotal) / static_cast<double>(timed),
                    static_cast<long long>(timed),
                    static_cast<int>(positions.size()),
                    maxResults,
                    static_cast<long long>(orderMismatches));

        std::printf("Query time per dictionary, total milliseconds over %lld lookups\n", static_cast<long long>(timed));
        std::size_t slot = 0;
        for (const QList<dict::DictionaryHandle> *list : {&wordDicts, &nameDicts}) {
            for (const dict::DictionaryHandle &handle : *list) {
                std::printf("  %-38s %-10s %9.1f ms  (%.3f ms per lookup)\n",
                            qPrintable(handle.name),
                            roleName(handle.type),
                            perDictionary[slot],
                            perDictionary[slot] / static_cast<double>(timed));
                ++slot;
            }
        }
        std::printf("\n");
    }

    if (parser.isSet(scaleOption)) {
        std::printf("Cold Engine::lookup() over the first k dictionaries, milliseconds\n");
        for (int k = 1; std::cmp_less_equal(k, snapshot->size()); k *= 2) {
            auto subset = std::make_shared<std::vector<dict::DictionaryHandle>>(
                snapshot->begin(), snapshot->begin() + std::min<std::size_t>(k, snapshot->size()));
            engine.setDictionaries(subset, manager.wordClasses());
            std::vector<double> samples;
            samples.reserve(positions.size());
            for (const Position &position : positions)
                samples.push_back(timeOnce(position, true).first);
            char label[64];
            std::snprintf(label, sizeof(label), "k = %d", k);
            printDistribution(label, distributionOf(samples));
        }
        engine.setDictionaries(snapshot, manager.wordClasses());
        std::vector<double> samples;
        samples.reserve(positions.size());
        for (const Position &position : positions)
            samples.push_back(timeOnce(position, true).first);
        char label[64];
        std::snprintf(label, sizeof(label), "k = %d (all)", static_cast<int>(snapshot->size()));
        printDistribution(label, distributionOf(samples));
        std::printf("\n");
    }

    return 0;
}

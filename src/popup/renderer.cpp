// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popup/renderer.h"

#include "popup/entrymodel.h"
#include "popup/renderoptions.h"
#include "popup/theme.h"

#include <QLatin1StringView>
#include <QStringList>

#include <KLocalizedString>

namespace maru::popup
{

using namespace Qt::Literals::StringLiterals;

namespace
{

// Foreground fractions based on meikipop's popup appearance (see NOTICE):
// 0.8 for the deconjugation path, 0.6
// for the frequency, 0.7 for the tag list. Qt's rich-text subset carries no opacity property,
// so each one is emitted as a color mixed toward the card background.
constexpr qreal deconjugationFraction = 0.8;
constexpr qreal frequencyFraction = 0.6;
constexpr qreal noteFraction = 0.7;

// The rank above which JL and meikipop both stop reporting a frequency, since a rank list
// pads every uncovered headword with it.
constexpr int frequencyRankLimit = 999999;

// Bottom margin of the last paragraph of every entry, in pixels. renderHtml() adds it to the
// top margin of the rule, which QTextDocument collapses with it to the larger of the two.
constexpr int entryBottomMargin = 2;

// Example words the kanji card lists, matching meikipop's cap.
constexpr int maxKanjiExamples = 3;

// The eight kanji card row labels, each defined once for renderHtml() and renderPlainText().
QString onLabel()
{
    return i18nc("@label kanji card, the Chinese-derived readings", "On");
}

QString kunLabel()
{
    return i18nc("@label kanji card, the native Japanese readings", "Kun");
}

QString nanoriLabel()
{
    return i18nc("@label kanji card, the readings used in personal names", "Nanori");
}

QString meaningsLabel()
{
    return i18nc("@label kanji card, the English meanings", "Meanings");
}

QString statsLabel()
{
    return i18nc("@label kanji card, the stroke count, the grade and the frequency rank", "Stats");
}

QString radicalsLabel()
{
    return i18nc("@label kanji card, the radicals of the character", "Radicals");
}

QString examplesLabel()
{
    return i18nc("@label kanji card, example words holding the character", "Examples");
}

QString componentsLabel()
{
    return i18nc("@label kanji card, the characters the kanji is built from", "Components");
}

// The stroke count as one sentence, so a translation orders the number and the noun itself.
QString strokeCountText(int strokeCount)
{
    return i18ncp("@item kanji card, %1 is the stroke count", "%1 stroke", "%1 strokes", strokeCount);
}

QString escaped(const QString &text)
{
    return text.toHtmlEscaped();
}

QString colorName(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

QString span(const QString &style, const QString &content)
{
    return u"<span style=\""_s % style % u"\">"_s % content % u"</span>"_s;
}

QString colorStyle(const QColor &color)
{
    return u"color:"_s % colorName(color) % u";"_s;
}

QString sizedColorStyle(int points, const QColor &color)
{
    return u"font-size:"_s % QString::number(points) % u"pt;"_s % colorStyle(color);
}

// A rounded tag pill. Qt's rich-text subset applies neither padding nor border-radius to an
// inline box, so the spacing inside a pill is two non-breaking spaces and the shape stays
// rectangular; the colors are nazeka's.
QString pill(const QString &text, const QColor &background, const Theme &theme)
{
    const QString style = u"background-color:"_s % colorName(background) % u";"_s % colorStyle(theme.tagTextColor);
    return span(style, u"&nbsp;"_s % text % u"&nbsp;"_s);
}

QString joinEscaped(const QStringList &values, QLatin1StringView separator)
{
    QStringList parts;
    parts.reserve(values.size());
    for (const QString &value : values) {
        parts.append(escaped(value));
    }
    return parts.join(separator);
}

// The senses of one entry, already selected and joined by the caller's separator.
QStringList renderSenses(const Entry &entry, const RenderOptions &options, const Theme &theme)
{
    QStringList rendered;
    rendered.reserve(entry.senses.size());
    int number = 0;
    for (const Sense &sense : entry.senses) {
        ++number;
        QStringList parts;
        if (options.showAllGlosses) {
            parts.append(u"<b>("_s % QString::number(number) % u")</b>"_s);
        }
        if (options.showPartOfSpeech && !sense.pos.isEmpty()) {
            parts.append(u"<i>("_s % joinEscaped(sense.pos, ", "_L1) % u")</i>"_s);
        }
        if (options.showTags) {
            QStringList tags = sense.misc;
            tags.append(sense.fields);
            tags.append(sense.dialects);
            if (!tags.isEmpty()) {
                parts.append(pill(u"["_s % joinEscaped(tags, ", "_L1) % u"]"_s, theme.tagMiscColor, theme));
            }
        }
        if (options.showOrthographyInfo && !sense.spellingRestrictions.isEmpty()) {
            const QString restriction = u"{"_s % joinEscaped(sense.spellingRestrictions, "、"_L1) % u"}"_s;
            parts.append(span(colorStyle(blend(theme.foreground, theme.background, noteFraction)), restriction));
        }

        const QStringList glosses = sense.glosses;
        if (!glosses.isEmpty()) {
            parts.append(options.showAllGlosses ? joinEscaped(glosses, ", "_L1) : escaped(glosses.constFirst()));
        }

        if (options.showTags && !sense.info.isEmpty()) {
            const QString note = u"<i>— "_s % escaped(sense.info) % u"</i>"_s;
            parts.append(span(colorStyle(blend(theme.foreground, theme.background, noteFraction)), note));
        }
        if (options.showTags && !sense.crossReferences.isEmpty()) {
            const QString references = u"⇒ "_s % joinEscaped(sense.crossReferences, "、"_L1);
            parts.append(span(colorStyle(blend(theme.foreground, theme.background, noteFraction)), references));
        }

        if (!parts.isEmpty()) {
            rendered.append(parts.join(u' '));
        }
    }
    return rendered;
}

QString renderHeader(const Entry &entry, int entryIndex, const RenderOptions &options, const Theme &theme)
{
    QStringList parts;
    parts.append(span(sizedColorStyle(theme.headerPt, theme.highlightWord), escaped(entry.headword)));

    if (options.showOrthographyInfo && !entry.orthographyInfo.isEmpty()) {
        const QString info = u"("_s % joinEscaped(entry.orthographyInfo, ", "_L1) % u")"_s;
        parts.append(span(colorStyle(blend(theme.foreground, theme.background, noteFraction)), info));
    }

    if (!entry.readings.isEmpty()) {
        // The reading size follows meikipop, which renders the reading two points below the
        // headword.
        const int readingPoints = qMax(6, theme.headerPt - 2);
        QString readings;
        for (qsizetype index = 0; index < entry.readings.size(); ++index) {
            if (index > 0) {
                readings += u"、"_s;
            }
            const QString &reading = entry.readings.at(index);
            const QString text = escaped(reading);
            // The anchor is what readingSpans() locates to paint the pitch contour over.
            const QString anchor =
                pitchAnchorName(entryIndex, static_cast<int>(index), static_cast<int>(reading.size()));
            readings += options.showPitchAccent ? u"<a name=\""_s % anchor % u"\">"_s % text % u"</a>"_s : text;
        }
        parts.append(span(sizedColorStyle(readingPoints, theme.highlightReading), u"["_s % readings % u"]"_s));
    }

    if (options.showAlternativeSpellings && !entry.alternativeSpellings.isEmpty()) {
        const QString spellings = u"("_s % joinEscaped(entry.alternativeSpellings, "、"_L1) % u")"_s;
        parts.append(span(colorStyle(theme.foreground), spellings));
    }

    if (options.showDeconjugation && !entry.deconjugationPaths.isEmpty()) {
        const QString paths = u"("_s % joinEscaped(entry.deconjugationPaths, "; "_L1) % u")"_s;
        const QString style = sizedColorStyle(qMax(6, theme.definitionPt - 2),
                                              blend(theme.foreground, theme.background, deconjugationFraction));
        parts.append(span(style, paths));
    }

    if (options.showFrequency) {
        QStringList frequency;
        if (entry.frequencyRank.has_value() && *entry.frequencyRank < frequencyRankLimit) {
            frequency.append(u"#"_s % QString::number(*entry.frequencyRank));
        }
        if (!entry.frequencyText.isEmpty()) {
            frequency.append(escaped(entry.frequencyText));
        }
        if (!frequency.isEmpty()) {
            const QString style = sizedColorStyle(qMax(6, theme.definitionPt - 2),
                                                  blend(theme.foreground, theme.background, frequencyFraction));
            parts.append(span(style, frequency.join(u' ')));
        }
    }

    if (options.showDictionaryName && !entry.dictionaryName.isEmpty()) {
        parts.append(span(colorStyle(theme.titleColor), escaped(entry.dictionaryName)));
    }

    return parts.join(u' ');
}

QString renderEntry(const Entry &entry, int entryIndex, const RenderOptions &options, const Theme &theme)
{
    const QString header = renderHeader(entry, entryIndex, options, theme);

    QString body;
    if (!entry.richTextGlossary.isEmpty()) {
        // Already in the rich-text subset, produced by the Yomitan structured-content
        // converter in dict/importers.
        body = entry.richTextGlossary;
    } else {
        const QStringList senses = renderSenses(entry, options, theme);
        body = senses.join(options.compactMode ? u"; "_s : u"<br>"_s);
    }

    const QString lastParagraph =
        u"<p style=\"margin-top:0px;margin-bottom:"_s % QString::number(entryBottomMargin) % u"px;\">"_s;
    if (body.isEmpty()) {
        return lastParagraph % header % u"</p>"_s;
    }
    if (options.compactMode && entry.richTextGlossary.isEmpty()) {
        // Compact mode puts the senses on the header line, as meikipop does.
        return lastParagraph % header % u" "_s % body % u"</p>"_s;
    }
    return u"<p style=\"margin-top:0px;margin-bottom:0px;\">"_s % header % u"</p>"_s % lastParagraph % body % u"</p>"_s;
}

QString kanjiRow(const QString &label, const QString &value, const Theme &theme)
{
    if (value.isEmpty()) {
        return {};
    }
    const QString head = span(colorStyle(theme.titleColor), escaped(label) % u":"_s);
    return u"<p style=\"margin-top:0px;margin-bottom:0px;\">"_s % head % u" "_s % value % u"</p>"_s;
}

QString renderKanji(const KanjiCard &kanji, const RenderOptions &options, const Theme &theme)
{
    QString inner;
    inner += u"<p style=\"margin-top:0px;margin-bottom:2px;\">"_s %
             span(sizedColorStyle(theme.headerPt + 6, theme.highlightWord), escaped(kanji.character)) % u"</p>"_s;

    inner += kanjiRow(onLabel(), joinEscaped(kanji.onReadings, "、"_L1), theme);
    inner += kanjiRow(kunLabel(), joinEscaped(kanji.kunReadings, "、"_L1), theme);
    inner += kanjiRow(nanoriLabel(), joinEscaped(kanji.nanoriReadings, "、"_L1), theme);
    inner += kanjiRow(meaningsLabel(), joinEscaped(kanji.meanings, ", "_L1), theme);

    if (options.showStrokeCountAndGrade) {
        QStringList stats;
        if (kanji.strokeCount > 0) {
            stats.append(strokeCountText(kanji.strokeCount));
        }
        if (kanji.grade.has_value()) {
            stats.append(gradeToText(*kanji.grade));
        }
        if (kanji.frequency.has_value()) {
            stats.append(u"#"_s % QString::number(*kanji.frequency));
        }
        inner += kanjiRow(statsLabel(), escaped(stats.join(", "_L1)), theme);
    }

    inner += kanjiRow(radicalsLabel(), joinEscaped(kanji.radicalNames, ", "_L1), theme);

    if (options.showKanjiExamples && !kanji.examples.isEmpty()) {
        const QStringList examples = kanji.examples.mid(0, maxKanjiExamples);
        inner += kanjiRow(examplesLabel(), joinEscaped(examples, "; "_L1), theme);
    }
    if (options.showKanjiComponents && !kanji.components.isEmpty()) {
        inner += kanjiRow(componentsLabel(), joinEscaped(kanji.components, ", "_L1), theme);
    }

    // A one-cell table rather than a bordered <div>: QTextDocument draws a border on a table
    // cell and draws none on a block element.
    return u"<table width=\"100%\" border=\"1\" cellspacing=\"0\" cellpadding=\"4\" bordercolor=\""_s %
           colorName(theme.highlightWord) % u"\"><tr><td>"_s % inner % u"</td></tr></table>"_s;
}

QStringList plainSenses(const Entry &entry, const RenderOptions &options)
{
    QStringList rendered;
    int number = 0;
    for (const Sense &sense : entry.senses) {
        ++number;
        QStringList parts;
        if (options.showAllGlosses) {
            parts.append(u"("_s % QString::number(number) % u")"_s);
        }
        if (options.showPartOfSpeech && !sense.pos.isEmpty()) {
            parts.append(u"("_s % sense.pos.join(", "_L1) % u")"_s);
        }
        if (options.showTags) {
            QStringList tags = sense.misc;
            tags.append(sense.fields);
            tags.append(sense.dialects);
            if (!tags.isEmpty()) {
                parts.append(u"["_s % tags.join(", "_L1) % u"]"_s);
            }
        }
        if (!sense.glosses.isEmpty()) {
            parts.append(options.showAllGlosses ? sense.glosses.join(", "_L1) : sense.glosses.constFirst());
        }
        if (options.showTags && !sense.info.isEmpty()) {
            parts.append(u"— "_s % sense.info);
        }
        if (!parts.isEmpty()) {
            rendered.append(parts.join(u' '));
        }
    }
    return rendered;
}

} // namespace

QString pitchAnchorName(int entryIndex, int readingIndex, int length)
{
    return u"pitch:"_s % QString::number(entryIndex) % u":"_s % QString::number(readingIndex) % u":"_s %
           QString::number(length);
}

bool parsePitchAnchorName(QStringView name, int *entryIndex, int *readingIndex, int *length)
{
    constexpr QLatin1StringView prefix{"pitch:"};
    if (!name.startsWith(prefix)) {
        return false;
    }
    const QList<QStringView> fields = name.sliced(prefix.size()).split(u':');
    if (fields.size() != 3) {
        return false;
    }
    bool entryOk = false;
    bool readingOk = false;
    bool lengthOk = false;
    const int entry = fields.at(0).toInt(&entryOk);
    const int reading = fields.at(1).toInt(&readingOk);
    const int runLength = fields.at(2).toInt(&lengthOk);
    if (!entryOk || !readingOk || !lengthOk || runLength <= 0) {
        return false;
    }
    if (entryIndex != nullptr) {
        *entryIndex = entry;
    }
    if (readingIndex != nullptr) {
        *readingIndex = reading;
    }
    if (length != nullptr) {
        *length = runLength;
    }
    return true;
}

QString gradeToText(int grade)
{
    // Each branch is one sentence with the grade as a placeholder, so a translation orders the
    // number and the list name itself.
    if (grade >= 1 && grade <= 6) {
        return i18nc("@item kanji card grade, %1 is a school year from 1 to 6", "%1 (Kyouiku)", grade);
    }
    if (grade == 8) {
        return i18nc("@item kanji card grade, the 2136 characters of the Jouyou list", "8 (Jouyou)");
    }
    if (grade == 9 || grade == 10) {
        return i18nc("@item kanji card grade, %1 is 9 or 10", "%1 (Jinmeiyou)", grade);
    }
    return i18nc("@item kanji card grade, a character outside every list", "Hyougai");
}

QString renderHtml(const PopupModel &model, const RenderOptions &options, const Theme &theme)
{
    if (model.isEmpty()) {
        return {};
    }

    QStringList blocks;
    blocks.reserve(model.entries.size() + 1);
    for (qsizetype index = 0; index < model.entries.size(); ++index) {
        blocks.append(renderEntry(model.entries.at(index), static_cast<int>(index), options, theme));
    }
    if (options.showKanji && model.kanji.has_value()) {
        blocks.append(renderKanji(*model.kanji, options, theme));
    }
    if (blocks.isEmpty()) {
        return {};
    }

    // The rule between entries uses 25 percent of the foreground color. QTextDocument
    // collapses adjacent margins to the larger value, so the top margin includes
    // entryBottomMargin as well as half the spacing. The resulting gap is Theme::entrySpacing.
    const int spacingAbove = qMax(0, theme.entrySpacing) / 2;
    const int spacingBelow = qMax(0, theme.entrySpacing) - spacingAbove;
    const QString rule = u"<hr style=\"margin-top:"_s % QString::number(entryBottomMargin + spacingAbove) %
                         u"px;margin-bottom:"_s % QString::number(spacingBelow) % u"px;\" color=\""_s %
                         colorName(blend(theme.foreground, theme.background, 0.25)) % u"\">"_s;

    const QString family =
        theme.fontFamily.isEmpty() ? QString{} : u"font-family:'"_s % escaped(theme.fontFamily) % u"';"_s;
    const QString bodyStyle =
        family % u"font-size:"_s % QString::number(theme.definitionPt) % u"pt;"_s % colorStyle(theme.foreground);

    return u"<div style=\""_s % bodyStyle % u"\">"_s % blocks.join(rule) % u"</div>"_s;
}

QString renderPlainText(const PopupModel &model, const RenderOptions &options)
{
    QStringList lines;
    for (const Entry &entry : model.entries) {
        QStringList header;
        header.append(entry.headword);
        if (!entry.readings.isEmpty()) {
            header.append(u"["_s % entry.readings.join("、"_L1) % u"]"_s);
        }
        if (options.showAlternativeSpellings && !entry.alternativeSpellings.isEmpty()) {
            header.append(u"("_s % entry.alternativeSpellings.join("、"_L1) % u")"_s);
        }
        if (options.showDeconjugation && !entry.deconjugationPaths.isEmpty()) {
            header.append(u"("_s % entry.deconjugationPaths.join("; "_L1) % u")"_s);
        }
        if (options.showFrequency && entry.frequencyRank.has_value() && *entry.frequencyRank < frequencyRankLimit) {
            header.append(u"#"_s % QString::number(*entry.frequencyRank));
        }
        if (options.showDictionaryName && !entry.dictionaryName.isEmpty()) {
            header.append(entry.dictionaryName);
        }
        lines.append(header.join(u' '));

        if (!entry.senses.isEmpty()) {
            const QStringList senses = plainSenses(entry, options);
            if (!senses.isEmpty()) {
                lines.append(senses.join(options.compactMode ? "; "_L1 : "\n"_L1));
            }
        }
    }

    if (options.showKanji && model.kanji.has_value()) {
        const KanjiCard &kanji = *model.kanji;
        lines.append(kanji.character);
        if (!kanji.onReadings.isEmpty()) {
            lines.append(onLabel() % u": "_s % kanji.onReadings.join("、"_L1));
        }
        if (!kanji.kunReadings.isEmpty()) {
            lines.append(kunLabel() % u": "_s % kanji.kunReadings.join("、"_L1));
        }
        if (!kanji.nanoriReadings.isEmpty()) {
            lines.append(nanoriLabel() % u": "_s % kanji.nanoriReadings.join("、"_L1));
        }
        if (!kanji.meanings.isEmpty()) {
            lines.append(meaningsLabel() % u": "_s % kanji.meanings.join(", "_L1));
        }
        if (options.showStrokeCountAndGrade) {
            QStringList stats;
            if (kanji.strokeCount > 0) {
                stats.append(strokeCountText(kanji.strokeCount));
            }
            if (kanji.grade.has_value()) {
                stats.append(gradeToText(*kanji.grade));
            }
            if (!stats.isEmpty()) {
                lines.append(statsLabel() % u": "_s % stats.join(", "_L1));
            }
        }
        if (!kanji.radicalNames.isEmpty()) {
            lines.append(radicalsLabel() % u": "_s % kanji.radicalNames.join(", "_L1));
        }
        if (options.showKanjiExamples && !kanji.examples.isEmpty()) {
            lines.append(examplesLabel() % u": "_s % kanji.examples.mid(0, maxKanjiExamples).join("; "_L1));
        }
        if (options.showKanjiComponents && !kanji.components.isEmpty()) {
            lines.append(componentsLabel() % u": "_s % kanji.components.join(", "_L1));
        }
    }

    return lines.join(u'\n');
}

} // namespace maru::popup

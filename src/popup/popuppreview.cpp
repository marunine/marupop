// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popup/popuppreview.h"

#include "popup/popupview.h"

#include <QPainter>
#include <QVBoxLayout>

namespace maru::popup
{

using namespace Qt::Literals::StringLiterals;

PopupModel samplePopupModel()
{
    PopupModel model;
    model.matchedText = u"読んだ"_s;

    Entry verb;
    verb.headword = u"読む"_s;
    verb.readings = QStringList{u"よむ"_s};
    verb.pitchPositions = QList<std::optional<quint8>>{std::optional<quint8>{1}};
    verb.alternativeSpellings = QStringList{u"讀む"_s};
    verb.deconjugationPaths = QStringList{u"～む→んだ; past"_s};
    verb.frequencyRank = 1042;
    verb.dictionaryName = u"JMdict"_s;
    verb.orthographyInfo = QStringList{u"oK"_s};
    Sense first;
    first.glosses = QStringList{u"to read"_s, u"to peruse"_s};
    first.pos = QStringList{u"v5m"_s, u"vt"_s};
    first.misc = QStringList{u"uk"_s};
    verb.senses.append(first);
    Sense second;
    second.glosses = QStringList{u"to guess"_s, u"to predict"_s};
    second.pos = QStringList{u"v5m"_s, u"vt"_s};
    second.fields = QStringList{u"math"_s};
    second.info = u"often in the form 先を読む"_s;
    verb.senses.append(second);
    model.entries.append(verb);

    Entry noun;
    noun.headword = u"日本"_s;
    noun.readings = QStringList{u"にほん"_s, u"にっぽん"_s};
    noun.pitchPositions = QList<std::optional<quint8>>{std::optional<quint8>{3}, std::optional<quint8>{3}};
    noun.frequencyRank = 88;
    noun.dictionaryName = u"JMdict"_s;
    Sense place;
    place.glosses = QStringList{u"Japan"_s};
    place.pos = QStringList{u"n"_s};
    place.dialects = QStringList{u"ksb"_s};
    place.crossReferences = QStringList{u"日本国"_s};
    noun.senses.append(place);
    model.entries.append(noun);

    KanjiCard kanji;
    kanji.character = u"本"_s;
    kanji.onReadings = QStringList{u"ホン"_s};
    kanji.kunReadings = QStringList{u"もと"_s};
    kanji.nanoriReadings = QStringList{u"まと"_s};
    kanji.meanings = QStringList{u"book"_s, u"origin"_s, u"main"_s};
    kanji.radicalNames = QStringList{u"き"_s};
    kanji.examples = QStringList{u"本 [ほん] book"_s, u"日本 [にほん] Japan"_s, u"本当 [ほんとう] truth"_s};
    kanji.components = QStringList{u"木 tree"_s, u"一 one"_s};
    kanji.strokeCount = 5;
    kanji.grade = 1;
    kanji.frequency = 10;
    model.kanji = kanji;

    return model;
}

PopupPreview::PopupPreview(QWidget *parent)
    : QWidget{parent}
{
    m_view = new PopupView{this};
    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(m_theme.padding, m_theme.padding, m_theme.padding, m_theme.padding);
    layout->addWidget(m_view);

    applyTheme();
    applyRenderOptions();
    m_view->setModel(samplePopupModel());
}

void PopupPreview::setModel(const PopupModel &model)
{
    m_view->setModel(model);
    update();
}

void PopupPreview::applyTheme()
{
    m_theme = themeFromSettings();
    m_view->setTheme(m_theme);
    if (auto *box = qobject_cast<QVBoxLayout *>(layout())) {
        box->setContentsMargins(m_theme.padding, m_theme.padding, m_theme.padding, m_theme.padding);
    }
    update();
}

void PopupPreview::applyRenderOptions()
{
    m_options = renderOptionsFromSettings();
    m_view->setRenderOptions(m_options);
    update();
}

QSize PopupPreview::sizeHint() const
{
    // Half of the card's own width bound, which is the widest a settings page can carry
    // without forcing the dialog wider.
    return QSize{qMin(m_theme.maxWidth, 380), 240};
}

void PopupPreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter{this};
    paintCard(painter, QRectF{rect()}, m_theme);
}

} // namespace maru::popup

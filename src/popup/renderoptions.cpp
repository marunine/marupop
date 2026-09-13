// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popup/renderoptions.h"

#include "marupopsettings.h"

namespace maru::popup
{

RenderOptions renderOptionsFromSettings()
{
    RenderOptions options;
    options.showAllGlosses = PopSettings::showAllGlosses();
    options.showDeconjugation = PopSettings::showDeconjugation();
    options.showPartOfSpeech = PopSettings::showPartOfSpeech();
    options.showTags = PopSettings::showTags();
    options.showFrequency = PopSettings::showFrequency();
    options.showKanji = PopSettings::showKanji();
    options.showKanjiExamples = PopSettings::showKanjiExamples();
    options.showKanjiComponents = PopSettings::showKanjiComponents();
    options.compactMode = PopSettings::compactMode();
    options.showAlternativeSpellings = PopSettings::showAlternativeSpellings();
    options.showPitchAccent = PopSettings::showPitchAccent();
    options.showDictionaryName = PopSettings::showDictionaryName();
    options.showOrthographyInfo = PopSettings::showOrthographyInfo();
    options.showStrokeCountAndGrade = PopSettings::showStrokeCountAndGrade();
    return options;
}

} // namespace maru::popup

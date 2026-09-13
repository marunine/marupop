// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Covers deconj/deconjugator.cpp's search and path rendering.
//
// The bulk of the suite is deconjugation_cases.h, the 23 word-class tables scraped out of JL's
// own NUnit suite (Apache-2.0, JL.Core.Tests/Deconjugation/*, commit 85ae02ee) by
// tools/gen-deconj-tests.py. One row is one JL [Test] method: the term, the
// (Text, LastTag) filter, and the whole rendered path set the surviving processes must produce.
// The rest of the file holds structural invariants, the adj-ix and くださる cases JL's suite has no
// file for, and a micro-benchmark gated behind MARUPOP_BENCH=1.
#include "deconj/deconjugator.h"
#include "deconjugation_cases.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QSet>
#include <QString>

#include <gtest/gtest.h>
#include <map>
#include <span>
#include <vector>

using namespace Qt::StringLiterals;
using maru::deconj::deconjugate;
using maru::deconj::deconjugationProcessText;
using maru::deconj::Form;
using maru::deconj::formattedProcess;
using maru::deconj::ProcessNode;
using maru::deconj::ProcessPtr;
using maru::deconj::properStepCount;
using maru::deconj::RuleSet;
using maru::deconj::testcases::Case;

namespace
{

const RuleSet &embeddedRuleSet()
{
    static const RuleSet rules = [] {
        QString errorString;
        std::optional<RuleSet> loaded = RuleSet::loadEmbedded(&errorString);
        EXPECT_TRUE(loaded.has_value()) << errorString.toStdString();
        return loaded ? std::move(*loaded) : RuleSet();
    }();
    return rules;
}

// The exact pipeline every JL test runs: deconjugate, keep the forms matching (Text, LastTag),
// render their processes.
QString renderedPath(QStringView input, QStringView lemma, QStringView tag)
{
    const std::vector<Form> forms = deconjugate(embeddedRuleSet(), input.toString());

    std::vector<ProcessPtr> processes;
    for (const Form &form : forms) {
        if (form.text == lemma && form.lastTag == tag)
            processes.push_back(form.process);
    }
    return deconjugationProcessText(processes);
}

void runTable(std::span<const Case> cases, const char *name)
{
    int failures = 0;
    for (const Case &testCase : cases) {
        const QString actual = renderedPath(testCase.input, testCase.lemma, testCase.tag);
        if (actual != testCase.expectedPath.toString()) {
            ++failures;
            ADD_FAILURE() << name << ": " << testCase.input.toString().toStdString() << " -> "
                          << testCase.lemma.toString().toStdString() << " [" << testCase.tag.toString().toStdString()
                          << "]\n  expected: " << testCase.expectedPath.toString().toStdString()
                          << "\n  actual:   " << actual.toStdString();
        }
        if (failures >= 10) {
            ADD_FAILURE() << name << ": stopping after 10 failures";
            return;
        }
    }
}

ProcessPtr node(QStringView detail, const ProcessPtr &parent = nullptr)
{
    const bool proper = !detail.isEmpty() && detail.front() != u'(';
    const int steps = parent ? properStepCount(parent) + (proper ? 1 : 0) : 1;
    return std::make_shared<const ProcessNode>(parent, detail, steps);
}

} // namespace

#define MARUPOP_DECONJ_TABLE(name, table)                                                                              \
    TEST(DeconjTable, name)                                                                                            \
    {                                                                                                                  \
        runTable(maru::deconj::testcases::table, #name);                                                               \
    }

MARUPOP_DECONJ_TABLE(AdjI, kAdjICases)
MARUPOP_DECONJ_TABLE(Cop, kCopCases)
MARUPOP_DECONJ_TABLE(V1, kV1Cases)
MARUPOP_DECONJ_TABLE(V1S, kV1SCases)
MARUPOP_DECONJ_TABLE(V4R, kV4RCases)
MARUPOP_DECONJ_TABLE(V5Aru, kV5AruCases)
MARUPOP_DECONJ_TABLE(V5B, kV5BCases)
MARUPOP_DECONJ_TABLE(V5G, kV5GCases)
MARUPOP_DECONJ_TABLE(V5K, kV5KCases)
MARUPOP_DECONJ_TABLE(V5KS, kV5KSCases)
MARUPOP_DECONJ_TABLE(V5M, kV5MCases)
MARUPOP_DECONJ_TABLE(V5N, kV5NCases)
MARUPOP_DECONJ_TABLE(V5R, kV5RCases)
MARUPOP_DECONJ_TABLE(V5RI, kV5RICases)
MARUPOP_DECONJ_TABLE(V5S, kV5SCases)
MARUPOP_DECONJ_TABLE(V5T, kV5TCases)
MARUPOP_DECONJ_TABLE(V5U, kV5UCases)
MARUPOP_DECONJ_TABLE(V5US, kV5USCases)
MARUPOP_DECONJ_TABLE(VK, kVKCases)
MARUPOP_DECONJ_TABLE(VSC, kVSCCases)
MARUPOP_DECONJ_TABLE(VSI, kVSICases)
MARUPOP_DECONJ_TABLE(VSS, kVSSCases)
MARUPOP_DECONJ_TABLE(VZ, kVZCases)

// JL's suite has no adj-ix file, though DeconjugatorUtils.ValidWordClasses lists the class and
// the rule set carries it on rules 10, 12, 14-18, 25 and 48. Every expectation below is read
// off those rules.
TEST(DeconjCoverageGap, AdjIx)
{
    // Rule 57 negative (ない consumed to stem-ku) then rule 12 (adverbial stem, く to い).
    EXPECT_EQ(renderedPath(u"よくない", u"よい", u"adj-ix"), u"～negative"_s);
    EXPECT_EQ(renderedPath(u"いくない", u"いい", u"adj-ix"), u"～negative"_s);
    EXPECT_EQ(renderedPath(u"よかった", u"よい", u"adj-ix"), u"～past"_s);
    EXPECT_EQ(renderedPath(u"よければ", u"よい", u"adj-ix"), u"～provisional conditional"_s);
    // Rule 10 volitional: かろう to い, listed for adj-i and adj-ix alike.
    EXPECT_EQ(renderedPath(u"よかろう", u"よい", u"adj-ix"), u"～volitional"_s);
    // Rule 25 noun form, then rule 22 seemingness.
    EXPECT_EQ(renderedPath(u"よさそう", u"よい", u"adj-ix"), u"～noun form→seemingness"_s);
    // Rule 48 rough casual: the ええ and っけえ spellings JL added on top of Nazeka's table.
    EXPECT_EQ(renderedPath(u"ええ", u"いい", u"adj-ix"), u"～rough casual"_s);
    EXPECT_EQ(renderedPath(u"かっけえ", u"かっこいい", u"adj-ix"), u"～rough casual"_s);
    EXPECT_EQ(renderedPath(u"かっけぇ", u"かっこいい", u"adj-ix"), u"～rough casual"_s);

    // The same surface forms also reach the adj-i class, which is what lets the lookup gate
    // pick whichever class the dictionary record carries.
    EXPECT_EQ(renderedPath(u"よくない", u"よい", u"adj-i"), u"～negative"_s);

    // An unconjugated adjective is never returned: the seed form carries no tag.
    EXPECT_TRUE(renderedPath(u"いい", u"いい", u"adj-ix").isEmpty());
}

// くださる is reached in JL's suite only through the 〜てください rule (rule 113). These pin the
// v5aru paths of the verb itself.
TEST(DeconjCoverageGap, Kudasaru)
{
    EXPECT_EQ(renderedPath(u"くださいます", u"くださる", u"v5aru"), u"～polite"_s);
    EXPECT_EQ(renderedPath(u"くださらない", u"くださる", u"v5aru"), u"～negative"_s);
    EXPECT_EQ(renderedPath(u"くださった", u"くださる", u"v5aru"), u"～past"_s);
    EXPECT_EQ(renderedPath(u"くださって", u"くださる", u"v5aru"), u"～te"_s);
    EXPECT_EQ(renderedPath(u"くだされば", u"くださる", u"v5aru"), u"～provisional conditional"_s);
    // Rule 113 turns 〜ください into the te stem, so 食べてください deconjugates the whole chain.
    EXPECT_EQ(renderedPath(u"たべてください", u"たべる", u"v1"), u"～polite request"_s);
}

TEST(DeconjSearch, StructuralInvariants)
{
    const QStringView inputs[] = {
        u"泣かなかった",
        u"食べさせられなかった",
        u"行ったら",
        u"よくなければ",
        u"来させられる",
        u"愛せた",
    };

    for (const QStringView input : inputs) {
        const std::vector<Form> forms = deconjugate(embeddedRuleSet(), input.toString());
        EXPECT_FALSE(forms.empty()) << input.toString().toStdString();

        // JL keeps every form that ties the minimum proper-step count for a (text, lastTag).
        // 愛せた therefore returns 愛する/vs-s twice, once per path. Assert the shared minimum
        // rather than requiring unique (text, lastTag) pairs.
        std::map<QString, int> stepsByKey;
        for (const Form &form : forms) {
            EXPECT_TRUE(maru::deconj::isTerminalWordClass(form.lastTag))
                << form.lastTag.toString().toStdString() << " for " << input.toString().toStdString();
            EXPECT_EQ(form.originalText, input.toString());
            EXPECT_NE(form.process, nullptr);
            EXPECT_LE(properStepCount(form.process), maru::deconj::kMaxProperSteps);
            EXPECT_NE(form.text, input.toString()) << "the seed form must never be returned";

            const QString key = form.text + u'/' + form.lastTag.toString();
            const auto [it, inserted] = stepsByKey.emplace(key, properStepCount(form.process));
            if (!inserted) {
                EXPECT_EQ(it->second, properStepCount(form.process))
                    << "forms sharing (text, lastTag) must tie on proper steps: " << key.toStdString();
            }
        }
    }
}

TEST(DeconjSearch, SpeculativeStemsAreLeftToTheDictionary)
{
    // Section 10.3 also lists "a non-conjugated word yields zero forms". The rules with an
    // empty con_end apply to any text, so にほん yields にほんる (masu stem) and にほんい
    // (stem) among others; no dictionary holds them, which is where they are dropped. What
    // does hold is that the seed text itself is never among the results.
    const std::vector<Form> forms = deconjugate(embeddedRuleSet(), u"にほん"_s);
    EXPECT_FALSE(forms.empty());
    for (const Form &form : forms)
        EXPECT_NE(form.text, u"にほん"_s);

    // A null QString and an empty one take the same path: every empty-con_end rule applies.
    EXPECT_EQ(deconjugate(embeddedRuleSet(), QString()).size(), deconjugate(embeddedRuleSet(), u""_s).size());
    EXPECT_EQ(deconjugate(embeddedRuleSet(), u""_s).size(), 4U);
}

TEST(DeconjSearch, OnlyFinalRuleFiresOnTheSeedFormAlone)
{
    // Two chained rules: the first tags the seed stem-x, the second consumes stem-x. As an
    // onlyfinalrule the second can never fire, because only the seed carries no tag.
    const QByteArray blocked = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":["B"],"con_end":["b"],)"
                                                 R"("dec_tag":["stem-x"],"con_tag":["seed"],"detail":"first"},)"
                                                 R"({"type":"onlyfinalrule","dec_end":["C"],"con_end":["B"],)"
                                                 R"("dec_tag":["v1"],"con_tag":["stem-x"],"detail":"second"}])");
    const std::optional<RuleSet> blockedRules = RuleSet::loadFromJson(blocked);
    ASSERT_TRUE(blockedRules.has_value());
    EXPECT_TRUE(deconjugate(*blockedRules, u"ab"_s).empty());

    const QByteArray allowed = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":["B"],"con_end":["b"],)"
                                                 R"("dec_tag":["stem-x"],"con_tag":["seed"],"detail":"first"},)"
                                                 R"({"type":"stdrule","dec_end":["C"],"con_end":["B"],)"
                                                 R"("dec_tag":["v1"],"con_tag":["stem-x"],"detail":"second"}])");
    const std::optional<RuleSet> allowedRules = RuleSet::loadFromJson(allowed);
    ASSERT_TRUE(allowedRules.has_value());
    const std::vector<Form> forms = deconjugate(*allowedRules, u"ab"_s);
    ASSERT_EQ(forms.size(), 1U);
    EXPECT_EQ(forms.front().text, u"aC"_s);
    // The path reads leaf to root: the rule nearest the lemma first, the surface-most last.
    EXPECT_EQ(formattedProcess(forms.front().process), u"second→first"_s);
}

TEST(DeconjSearch, NeverFinalRuleSkipsTheSeedForm)
{
    const QByteArray blocked = QByteArrayLiteral(R"([{"type":"neverfinalrule","dec_end":["B"],"con_end":["b"],)"
                                                 R"("dec_tag":["v1"],"con_tag":["seed"],"detail":"step"}])");
    const std::optional<RuleSet> blockedRules = RuleSet::loadFromJson(blocked);
    ASSERT_TRUE(blockedRules.has_value());
    EXPECT_TRUE(deconjugate(*blockedRules, u"ab"_s).empty());

    const QByteArray allowed = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":["B"],"con_end":["b"],)"
                                                 R"("dec_tag":["v1"],"con_tag":["seed"],"detail":"step"}])");
    const std::optional<RuleSet> allowedRules = RuleSet::loadFromJson(allowed);
    ASSERT_TRUE(allowedRules.has_value());
    EXPECT_EQ(deconjugate(*allowedRules, u"ab"_s).size(), 1U);
}

TEST(DeconjSearch, RewriteRuleNeedsWholeTextEquality)
{
    const QByteArray json = QByteArrayLiteral(R"([{"type":"rewriterule","dec_end":["cd"],"con_end":["ab"],)"
                                              R"("dec_tag":["v1"],"con_tag":["seed"],"detail":"rewrite"}])");
    const std::optional<RuleSet> rules = RuleSet::loadFromJson(json);
    ASSERT_TRUE(rules.has_value());

    const std::vector<Form> whole = deconjugate(*rules, u"ab"_s);
    ASSERT_EQ(whole.size(), 1U);
    EXPECT_EQ(whole.front().text, u"cd"_s);

    // The same con_end is a suffix of "xab", which a stdrule would consume and a rewriterule
    // must not.
    EXPECT_TRUE(deconjugate(*rules, u"xab"_s).empty());
}

TEST(DeconjSearch, ProperStepCutoffTerminatesASelfApplyingRule)
{
    // dec_end equals con_end, so the text never shrinks and the rule can consume its own
    // output. Only the 8-proper-step cutoff ends the search.
    const QByteArray json = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":["a"],"con_end":["a"],)"
                                              R"("dec_tag":["v1"],"con_tag":["v1"],"detail":"loop"}])");
    const std::optional<RuleSet> rules = RuleSet::loadFromJson(json);
    ASSERT_TRUE(rules.has_value());

    const std::vector<Form> forms = deconjugate(*rules, u"aaa"_s);
    ASSERT_EQ(forms.size(), 1U);
    EXPECT_EQ(properStepCount(forms.front().process), 1);
    EXPECT_EQ(formattedProcess(forms.front().process), u"loop"_s);
}

TEST(DeconjRendering, DetailVisibility)
{
    // An empty detail never shows.
    EXPECT_EQ(formattedProcess(node(u"")), QString());
    EXPECT_EQ(formattedProcess(node(u"leaf", node(u""))), u"leaf"_s);
    EXPECT_EQ(formattedProcess(node(u"", node(u"root"))), u"root"_s);

    // A parenthesised detail is hidden mid-chain and shown without its parentheses at the root.
    EXPECT_EQ(formattedProcess(node(u"(masu stem)")), u"masu stem"_s);
    EXPECT_EQ(formattedProcess(node(u"leaf", node(u"(masu stem)"))), u"leaf→masu stem"_s);
    EXPECT_EQ(formattedProcess(node(u"(masu stem)", node(u"root"))), u"root"_s);

    // Parentheses inside a detail rather than around it print verbatim.
    EXPECT_EQ(formattedProcess(node(u"toku (for now)")), u"toku (for now)"_s);
    EXPECT_EQ(formattedProcess(node(u"honorific (ksb)", node(u"teru"))), u"honorific (ksb)→teru"_s);

    // A hand-written rule set RuleSet::loadFromJson() accepts can carry an unclosed detail,
    // which holds one parenthesis and prints verbatim at every position in the chain.
    EXPECT_EQ(formattedProcess(node(u"(")), u"("_s);
    EXPECT_EQ(formattedProcess(node(u"(x")), u"(x"_s);
    EXPECT_EQ(formattedProcess(node(u"leaf", node(u"("))), u"leaf→("_s);
    EXPECT_EQ(formattedProcess(node(u"leaf", node(u"(x"))), u"leaf→(x"_s);
    EXPECT_EQ(formattedProcess(node(u"(", node(u"root"))), u"(→root"_s);

    // "()" names no stem and is skipped like an empty detail.
    EXPECT_EQ(formattedProcess(node(u"()")), QString());
    EXPECT_EQ(formattedProcess(node(u"leaf", node(u"()"))), u"leaf"_s);

    // A closed detail of 4 characters is stripped to its 2 inner characters at the root.
    EXPECT_EQ(formattedProcess(node(u"(xy)")), u"xy"_s);
    EXPECT_EQ(formattedProcess(node(u"leaf", node(u"(xy)"))), u"leaf→xy"_s);
    EXPECT_EQ(formattedProcess(node(u"(xy)", node(u"root"))), u"root"_s);

    EXPECT_EQ(formattedProcess(nullptr), QString());
}

TEST(DeconjRendering, PathSeparators)
{
    const ProcessPtr first = node(u"negative", node(u"past"));
    const ProcessPtr second = node(u"slurred negative");

    EXPECT_EQ(deconjugationProcessText(std::span<const ProcessPtr>()), QString());
    EXPECT_EQ(deconjugationProcessText(std::vector<ProcessPtr>{first}), u"～negative→past"_s);
    EXPECT_EQ(deconjugationProcessText(std::vector<ProcessPtr>{first, second}), u"～negative→past; slurred negative"_s);

    // A single path that renders to nothing gives no text at all.
    EXPECT_EQ(deconjugationProcessText(std::vector<ProcessPtr>{node(u"")}), QString());

    // U+FF5E marks the first path that renders, so a leading path rendering to nothing leaves
    // the prefix on the path after it. lookup::deconjugationProcessText() (src/lookup/query.cpp)
    // reaches the same output, because its caller drops an empty rendering before building the
    // list.
    EXPECT_EQ(deconjugationProcessText(std::vector<ProcessPtr>{node(u""), first}), u"～negative→past"_s);
    // A leading path of one mid-chain parenthesised stem over an empty root renders to nothing
    // as well.
    EXPECT_EQ(deconjugationProcessText(std::vector<ProcessPtr>{node(u"(masu stem)", node(u"")), first}),
              u"～negative→past"_s);
    EXPECT_EQ(deconjugationProcessText(std::vector<ProcessPtr>{node(u""), first, node(u""), second}),
              u"～negative→past; slurred negative"_s);
    EXPECT_EQ(deconjugationProcessText(std::vector<ProcessPtr>{node(u""), node(u"")}), QString());
}

TEST(DeconjRendering, MultiplePathsOnOneForm)
{
    // Two forms with distinct deconjugation paths: both render as a "; " joined pair.
    EXPECT_EQ(renderedPath(u"泣かせん", u"泣く", u"v5k"), u"～causative→slurred; causative→slurred negative"_s);
    EXPECT_EQ(renderedPath(u"泣いてくれ", u"泣く", u"v5k"),
              u"～statement/request→imperative; statement/request→masu stem"_s);
}

// Times deconjugate() over every input of the transcribed tables. Off by default because it
// runs the whole corpus several times; MARUPOP_BENCH=1 turns it on.
TEST(DeconjBenchmark, DeconjugateThroughput)
{
    if (qEnvironmentVariable("MARUPOP_BENCH") != u"1"_s)
        GTEST_SKIP() << "set MARUPOP_BENCH=1 to run the deconjugation benchmark";

    std::vector<QString> inputs;
    const auto collect = [&inputs](std::span<const Case> cases) {
        for (const Case &testCase : cases)
            inputs.push_back(testCase.input.toString());
    };
    collect(maru::deconj::testcases::kAdjICases);
    collect(maru::deconj::testcases::kCopCases);
    collect(maru::deconj::testcases::kV1Cases);
    collect(maru::deconj::testcases::kV1SCases);
    collect(maru::deconj::testcases::kV4RCases);
    collect(maru::deconj::testcases::kV5AruCases);
    collect(maru::deconj::testcases::kV5BCases);
    collect(maru::deconj::testcases::kV5GCases);
    collect(maru::deconj::testcases::kV5KCases);
    collect(maru::deconj::testcases::kV5KSCases);
    collect(maru::deconj::testcases::kV5MCases);
    collect(maru::deconj::testcases::kV5NCases);
    collect(maru::deconj::testcases::kV5RCases);
    collect(maru::deconj::testcases::kV5RICases);
    collect(maru::deconj::testcases::kV5SCases);
    collect(maru::deconj::testcases::kV5TCases);
    collect(maru::deconj::testcases::kV5UCases);
    collect(maru::deconj::testcases::kV5USCases);
    collect(maru::deconj::testcases::kVKCases);
    collect(maru::deconj::testcases::kVSCCases);
    collect(maru::deconj::testcases::kVSICases);
    collect(maru::deconj::testcases::kVSSCases);
    collect(maru::deconj::testcases::kVZCases);

    constexpr int kRounds = 5;
    std::size_t forms = 0;
    QElapsedTimer timer;
    timer.start();
    for (int round = 0; round < kRounds; ++round) {
        for (const QString &input : inputs)
            forms += deconjugate(embeddedRuleSet(), input).size();
    }
    const qint64 elapsedNs = timer.nsecsElapsed();

    const double calls = static_cast<double>(inputs.size()) * kRounds;
    std::printf("deconjugate: %zu inputs x %d rounds, %.2f us/call, %zu forms\n",
                inputs.size(),
                kRounds,
                static_cast<double>(elapsedNs) / calls / 1000.0,
                forms);
    EXPECT_GT(forms, 0U);
}

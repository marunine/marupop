// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Covers deconj/deconjugator.cpp's loading of the JL rule set compiled into the
// binary at :/marupop/deconjugation_rules.json.
#include "deconj/deconjugator.h"

#include <QByteArray>
#include <QFile>
#include <QSet>
#include <QString>

#include <gtest/gtest.h>
#include <map>

using namespace Qt::StringLiterals;
using maru::deconj::Rule;
using maru::deconj::RuleSet;
using maru::deconj::RuleType;

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

QByteArray embeddedJson()
{
    QFile file(u":/marupop/deconjugation_rules.json"_s);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

} // namespace

TEST(DeconjRuleSet, EmbeddedRuleSetLoads)
{
    EXPECT_EQ(embeddedRuleSet().ruleCount(), 156);
    EXPECT_EQ(embeddedRuleSet().virtualRules().size(), 609U);
}

TEST(DeconjRuleSet, VirtualRuleTypeHistogram)
{
    std::map<RuleType, int> histogram;
    for (const Rule &rule : embeddedRuleSet().virtualRules())
        ++histogram[rule.type];

    // The 156 rule objects are 91 stdrule, 47 onlyfinalrule, 10 rewriterule and 8
    // neverfinalrule; expanding their parallel arrays gives the counts below.
    EXPECT_EQ(histogram[RuleType::Standard], 224);
    EXPECT_EQ(histogram[RuleType::OnlyFinal], 289);
    EXPECT_EQ(histogram[RuleType::Rewrite], 10);
    EXPECT_EQ(histogram[RuleType::NeverFinal], 86);
}

TEST(DeconjRuleSet, EmptyConEndBucket)
{
    // The rule set has 10 virtual rules with an empty con_end across five rule objects
    // (indices 2, 6, 8, 9 and 17). A source container's capacity hint is not the rule count.
    EXPECT_EQ(embeddedRuleSet().rulesWithEmptyConEnd().allRules.size(), 10U);

    for (const Rule &rule : embeddedRuleSet().rulesWithEmptyConEnd().allRules)
        EXPECT_TRUE(rule.conEnd.isEmpty());
}

TEST(DeconjRuleSet, BucketsCoverEveryNonEmptyConEnd)
{
    std::size_t bucketed = 0;
    QSet<char16_t> lastCharacters;
    for (const Rule &rule : embeddedRuleSet().virtualRules()) {
        if (rule.conEnd.isEmpty())
            continue;
        lastCharacters.insert(rule.conEnd.back().unicode());
        const maru::deconj::RuleBucket *bucket = embeddedRuleSet().bucketForLastChar(rule.conEnd.back());
        ASSERT_NE(bucket, nullptr);
        ++bucketed;
    }
    EXPECT_EQ(bucketed, 599U);
    EXPECT_EQ(lastCharacters.size(), 59);

    EXPECT_EQ(embeddedRuleSet().bucketForLastChar(QChar(u'X')), nullptr);
}

TEST(DeconjRuleSet, RulesByConTagPartitionsEveryBucket)
{
    for (const Rule &rule : embeddedRuleSet().virtualRules()) {
        const maru::deconj::RuleBucket &bucket = rule.conEnd.isEmpty()
                                                     ? embeddedRuleSet().rulesWithEmptyConEnd()
                                                     : *embeddedRuleSet().bucketForLastChar(rule.conEnd.back());

        std::size_t indexed = 0;
        for (const auto &[tag, rules] : bucket.rulesByConTag)
            indexed += rules.size();
        EXPECT_EQ(indexed, bucket.allRules.size());
    }
}

TEST(DeconjRuleSet, NoDeadEndStemTag)
{
    // Every tag a rule produces is either one of the 24 terminal word classes, which end the
    // search, or the con_tag of some other rule, which continues it. A dec_tag that is neither
    // would be a stem no rule can consume.
    QSet<QString> conTags;
    for (const Rule &rule : embeddedRuleSet().virtualRules())
        conTags.insert(rule.conTag.toString());

    for (const Rule &rule : embeddedRuleSet().virtualRules()) {
        if (maru::deconj::isTerminalWordClass(rule.decTag))
            continue;
        EXPECT_TRUE(conTags.contains(rule.decTag.toString()))
            << "dead-end dec_tag: " << rule.decTag.toString().toStdString();
    }
}

TEST(DeconjRuleSet, TerminalWordClasses)
{
    EXPECT_EQ(maru::deconj::terminalWordClasses().size(), 24U);
    for (const QStringView tag : maru::deconj::terminalWordClasses())
        EXPECT_TRUE(maru::deconj::isTerminalWordClass(tag));

    EXPECT_TRUE(maru::deconj::isTerminalWordClass(u"adj-ix"));
    EXPECT_TRUE(maru::deconj::isTerminalWordClass(u"v5k-s"));
    EXPECT_FALSE(maru::deconj::isTerminalWordClass(u"stem-te"));
    EXPECT_FALSE(maru::deconj::isTerminalWordClass(u""));
    EXPECT_FALSE(maru::deconj::isTerminalWordClass(QStringView()));
}

TEST(DeconjRuleSet, LoadRejectsMalformedInput)
{
    struct Rejection
    {
        const char *what;
        QByteArray json;
    };

    const Rejection rejections[] = {
        {.what = "malformed JSON", .json = QByteArrayLiteral("[")},
        {.what = "top level is not an array", .json = QByteArrayLiteral("{}")},
        {.what = "element is not an object", .json = QByteArrayLiteral("[1]")},
        {.what = "unknown rule type",
         .json = QByteArrayLiteral(R"([{"type":"contextrule","dec_end":["a"],"con_end":["b"],)"
                                   R"("dec_tag":["v1"],"con_tag":["v1"],"detail":"x"}])")},
        {.what = "missing type",
         .json = QByteArrayLiteral(
             R"([{"dec_end":["a"],"con_end":["b"],"dec_tag":["v1"],"con_tag":["v1"],"detail":"x"}])")},
        {.what = "missing detail",
         .json = QByteArrayLiteral(
             R"([{"type":"stdrule","dec_end":["a"],"con_end":["b"],"dec_tag":["v1"],"con_tag":["v1"]}])")},
        {.what = "dec_end is not an array",
         .json = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":"a","con_end":["b"],)"
                                   R"("dec_tag":["v1"],"con_tag":["v1"],"detail":"x"}])")},
        {.what = "dec_end holds a non-string",
         .json = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":[1],"con_end":["b"],)"
                                   R"("dec_tag":["v1"],"con_tag":["v1"],"detail":"x"}])")},
        {.what = "dec_end shorter than con_end",
         .json = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":["a"],"con_end":["b","c"],)"
                                   R"("dec_tag":["v1"],"con_tag":["v1"],"detail":"x"}])")},
        {.what = "dec_tag length is neither 1 nor con_end's length",
         .json = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":["a","b","c"],"con_end":["d","e","f"],)"
                                   R"("dec_tag":["v1","v1"],"con_tag":["v1"],"detail":"x"}])")},
        {.what = "con_tag length is neither 1 nor con_end's length",
         .json = QByteArrayLiteral(R"([{"type":"stdrule","dec_end":["a","b","c"],"con_end":["d","e","f"],)"
                                   R"("dec_tag":["v1"],"con_tag":["v1","v1"],"detail":"x"}])")},
    };

    for (const Rejection &rejection : rejections) {
        QString errorString;
        EXPECT_FALSE(RuleSet::loadFromJson(rejection.json, &errorString).has_value()) << rejection.what;
        EXPECT_FALSE(errorString.isEmpty()) << rejection.what;
    }
}

TEST(DeconjRuleSet, LoadAcceptsBroadcastTagArrays)
{
    QString errorString;
    const std::optional<RuleSet> rules =
        RuleSet::loadFromJson(QByteArrayLiteral(R"([{"type":"stdrule","dec_end":["a","b"],"con_end":["c","d"],)"
                                                R"("dec_tag":["v1"],"con_tag":["v1","v5r"],"detail":"x"}])"),
                              &errorString);
    ASSERT_TRUE(rules.has_value()) << errorString.toStdString();
    ASSERT_EQ(rules->virtualRules().size(), 2U);
    EXPECT_EQ(rules->virtualRules()[0].decTag, u"v1");
    EXPECT_EQ(rules->virtualRules()[1].decTag, u"v1");
    EXPECT_EQ(rules->virtualRules()[0].conTag, u"v1");
    EXPECT_EQ(rules->virtualRules()[1].conTag, u"v5r");
    EXPECT_EQ(rules->ruleCount(), 1);
}

TEST(DeconjRuleSet, LoadFromJsonMatchesLoadEmbedded)
{
    QString errorString;
    const std::optional<RuleSet> rules = RuleSet::loadFromJson(embeddedJson(), &errorString);
    ASSERT_TRUE(rules.has_value()) << errorString.toStdString();
    EXPECT_EQ(rules->ruleCount(), embeddedRuleSet().ruleCount());
    EXPECT_EQ(rules->virtualRules().size(), embeddedRuleSet().virtualRules().size());
}

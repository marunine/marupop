// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Ported from JL (Apache-2.0), JL.Core/Deconjugation/*.cs and JL.Core/Lookup/RuleBucket.cs at
// commit 85ae02eeb84f378387f48c12e7b468390a9f2007.
#include "deconjugator.h"

#include "core/logging.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <utility>

using namespace Qt::StringLiterals;

namespace maru::deconj
{

namespace
{

// DeconjugatorUtils.ValidWordClasses. These are JMdict part-of-speech codes; the lookup gate
// tests a deconjugated form's lastTag against the word classes of a candidate record.
constexpr std::array<QStringView, 24> kTerminalWordClasses{
    u"adj-i", u"adj-ix", u"cop",   u"v1",  u"v1-s", u"v4r", u"v5aru", u"v5b", u"v5g",  u"v5k",  u"v5k-s", u"v5m",
    u"v5n",   u"v5r",    u"v5r-i", u"v5s", u"v5t",  u"v5u", u"v5u-s", u"vk",  u"vs-c", u"vs-i", u"vs-s",  u"vz",
};

[[nodiscard]] std::optional<RuleType> ruleTypeFromString(QStringView text)
{
    if (text == u"stdrule")
        return RuleType::Standard;
    if (text == u"onlyfinalrule")
        return RuleType::OnlyFinal;
    if (text == u"rewriterule")
        return RuleType::Rewrite;
    if (text == u"neverfinalrule")
        return RuleType::NeverFinal;
    return std::nullopt;
}

// Every distinct string of the rule set, concatenated into one QString. The rules hold views
// into it, so a Rule is six pointers and the whole set is one allocation plus the index.
class StringArena
{
public:
    void reserve(const QString &text)
    {
        m_offsets.emplace(text, Span{.offset = 0, .length = 0});
    }

    void build()
    {
        qsizetype total = 0;
        for (const auto &[text, span] : m_offsets)
            total += text.size();

        m_storage.reserve(total);
        for (auto &[text, span] : m_offsets) {
            span = Span{.offset = m_storage.size(), .length = text.size()};
            m_storage.append(text);
        }
    }

    [[nodiscard]] QStringView view(const QString &text) const
    {
        const auto it = m_offsets.find(text);
        Q_ASSERT(it != m_offsets.end());
        return QStringView{m_storage}.sliced(it->second.offset, it->second.length);
    }

private:
    struct Span
    {
        qsizetype offset;
        qsizetype length;
    };

    QString m_storage;
    std::map<QString, Span> m_offsets;
};

// One rule object as the JSON holds it, before virtual-rule expansion.
struct RawRule
{
    RuleType type = RuleType::Standard;
    QStringList decEnds;
    QStringList conEnds;
    QStringList decTags;
    QStringList conTags;
    QString detail;
};

[[nodiscard]] bool
readStringArray(const QJsonObject &object, QLatin1StringView key, QStringList *out, int index, QString *errorString)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        if (errorString != nullptr)
            *errorString = u"rule %1: field \"%2\" is missing or is not an array"_s.arg(index).arg(key);
        return false;
    }

    const QJsonArray array = value.toArray();
    out->reserve(array.size());
    for (const QJsonValueConstRef element : array) {
        if (!element.isString()) {
            if (errorString != nullptr)
                *errorString = u"rule %1: field \"%2\" holds a non-string element"_s.arg(index).arg(key);
            return false;
        }
        out->append(element.toString());
    }
    return true;
}

[[nodiscard]] bool readRawRule(const QJsonValue &value, int index, RawRule *out, QString *errorString)
{
    if (!value.isObject()) {
        if (errorString != nullptr)
            *errorString = u"rule %1: element is not an object"_s.arg(index);
        return false;
    }

    const QJsonObject object = value.toObject();
    const QJsonValue typeValue = object.value("type"_L1);
    if (!typeValue.isString()) {
        if (errorString != nullptr)
            *errorString = u"rule %1: field \"type\" is missing or is not a string"_s.arg(index);
        return false;
    }

    const QString typeText = typeValue.toString();
    const std::optional<RuleType> type = ruleTypeFromString(typeText);
    if (!type) {
        if (errorString != nullptr)
            *errorString = u"rule %1: unknown rule type \"%2\""_s.arg(index).arg(typeText);
        return false;
    }
    out->type = *type;

    const QJsonValue detailValue = object.value("detail"_L1);
    if (!detailValue.isString()) {
        if (errorString != nullptr)
            *errorString = u"rule %1: field \"detail\" is missing or is not a string"_s.arg(index);
        return false;
    }
    out->detail = detailValue.toString();

    if (!readStringArray(object, "dec_end"_L1, &out->decEnds, index, errorString) ||
        !readStringArray(object, "con_end"_L1, &out->conEnds, index, errorString) ||
        !readStringArray(object, "dec_tag"_L1, &out->decTags, index, errorString) ||
        !readStringArray(object, "con_tag"_L1, &out->conTags, index, errorString)) {
        return false;
    }

    // The expansion loop is driven by con_end and indexes dec_end with the same index, so a
    // shorter dec_end is a data bug rather than a broadcast.
    if (out->decEnds.size() != out->conEnds.size()) {
        if (errorString != nullptr) {
            *errorString = u"rule %1: dec_end holds %2 entries and con_end holds %3"_s.arg(index)
                               .arg(out->decEnds.size())
                               .arg(out->conEnds.size());
        }
        return false;
    }

    // A tag array either names one tag broadcast over every index of con_end, or one tag per
    // index; any other length is a data bug.
    const auto tagArrays = {std::pair{"dec_tag"_L1, &out->decTags}, std::pair{"con_tag"_L1, &out->conTags}};
    const auto *const offender = std::ranges::find_if(tagArrays, [out](const auto &entry) {
        return entry.second->size() != 1 && entry.second->size() != out->conEnds.size();
    });
    if (offender != tagArrays.end()) {
        if (errorString != nullptr) {
            *errorString = u"rule %1: %2 holds %3 entries, which is neither 1 nor con_end's %4"_s.arg(index)
                               .arg(offender->first)
                               .arg(offender->second->size())
                               .arg(out->conEnds.size());
        }
        return false;
    }

    return true;
}

} // namespace

bool isTerminalWordClass(QStringView tag)
{
    return std::ranges::binary_search(kTerminalWordClasses, tag);
}

std::span<const QStringView> terminalWordClasses()
{
    return kTerminalWordClasses;
}

struct RuleSet::Private
{
    StringArena arena;
    std::vector<Rule> virtualRules;
    std::unordered_map<char16_t, RuleBucket> byLastConEndChar;
    RuleBucket emptyConEnd;
    int ruleCount = 0;
    qsizetype longestConEnd = 0;
};

RuleSet::RuleSet()
    : m_data(std::make_unique<Private>())
{}

RuleSet::~RuleSet() = default;

RuleSet::RuleSet(RuleSet &&) noexcept = default;

RuleSet &RuleSet::operator=(RuleSet &&) noexcept = default;

std::optional<RuleSet> RuleSet::loadFromJson(const QByteArray &json, QString *errorString)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorString != nullptr)
            *errorString = u"JSON parse error at offset %1: %2"_s.arg(parseError.offset).arg(parseError.errorString());
        return std::nullopt;
    }
    if (!document.isArray()) {
        if (errorString != nullptr)
            *errorString = u"the rule set is not a JSON array"_s;
        return std::nullopt;
    }

    const QJsonArray array = document.array();
    std::vector<RawRule> rawRules;
    rawRules.reserve(array.size());
    for (int i = 0; i < array.size(); ++i) {
        RawRule rule;
        if (!readRawRule(array.at(i), i, &rule, errorString))
            return std::nullopt;
        rawRules.push_back(std::move(rule));
    }

    RuleSet ruleSet;
    Private &data = *ruleSet.m_data;
    data.ruleCount = static_cast<int>(rawRules.size());

    for (const RawRule &rule : rawRules) {
        data.arena.reserve(rule.detail);
        for (const QStringList *list : {&rule.decEnds, &rule.conEnds, &rule.decTags, &rule.conTags}) {
            for (const QString &text : *list)
                data.arena.reserve(text);
        }
    }
    data.arena.build();

    for (const RawRule &rule : rawRules) {
        const QStringView detail = data.arena.view(rule.detail);
        const bool singleConTag = rule.conTags.size() == 1;
        const bool singleDecTag = rule.decTags.size() == 1;

        for (qsizetype i = 0; i < rule.conEnds.size(); ++i) {
            const Rule virtualRule{.type = rule.type,
                                   .decEnd = data.arena.view(rule.decEnds.at(i)),
                                   .conEnd = data.arena.view(rule.conEnds.at(i)),
                                   .decTag = data.arena.view(rule.decTags.at(singleDecTag ? 0 : i)),
                                   .conTag = data.arena.view(rule.conTags.at(singleConTag ? 0 : i)),
                                   .detail = detail};
            data.virtualRules.push_back(virtualRule);
            data.longestConEnd = std::max(data.longestConEnd, virtualRule.conEnd.size());

            RuleBucket &bucket = virtualRule.conEnd.isEmpty()
                                     ? data.emptyConEnd
                                     : data.byLastConEndChar[virtualRule.conEnd.back().unicode()];
            bucket.allRules.push_back(virtualRule);
            bucket.rulesByConTag[virtualRule.conTag].push_back(virtualRule);
        }
    }

    qCDebug(logDeconj,
            "loaded %d rules expanding to %zu virtual rules over %zu buckets",
            data.ruleCount,
            data.virtualRules.size(),
            data.byLastConEndChar.size());
    return ruleSet;
}

std::optional<RuleSet> RuleSet::loadEmbedded(QString *errorString)
{
    QFile file(u":/marupop/deconjugation_rules.json"_s);
    if (!file.open(QIODevice::ReadOnly)) {
        const QString reason = u"cannot open :/marupop/deconjugation_rules.json: %1"_s.arg(file.errorString());
        qCWarning(logDeconj, "%s", qUtf8Printable(reason));
        if (errorString != nullptr)
            *errorString = reason;
        return std::nullopt;
    }

    QString reason;
    std::optional<RuleSet> ruleSet = loadFromJson(file.readAll(), &reason);
    if (!ruleSet) {
        qCWarning(logDeconj, "cannot load the embedded rule set: %s", qUtf8Printable(reason));
        if (errorString != nullptr)
            *errorString = reason;
    }
    return ruleSet;
}

const RuleBucket *RuleSet::bucketForLastChar(QChar lastCharacter) const
{
    const auto it = m_data->byLastConEndChar.find(lastCharacter.unicode());
    return it == m_data->byLastConEndChar.end() ? nullptr : &it->second;
}

const RuleBucket &RuleSet::rulesWithEmptyConEnd() const
{
    return m_data->emptyConEnd;
}

bool RuleSet::isTerminalWordClass(QStringView tag) const
{
    return deconj::isTerminalWordClass(tag);
}

int RuleSet::ruleCount() const
{
    return m_data->ruleCount;
}

qsizetype RuleSet::longestConEnd() const
{
    return m_data->longestConEnd;
}

std::span<const Rule> RuleSet::virtualRules() const
{
    return m_data->virtualRules;
}

namespace
{

// JL's DiscoveredFormsContain. The parent is compared by pointer identity, matching
// ProcessNode.Equals, and the step count is compared against the parent's own count, which is
// the comparison JL performs.
[[nodiscard]] bool discoveredFormsContain(const std::vector<Form> &discovered,
                                          QStringView stem,
                                          const Rule &rule,
                                          const ProcessPtr &parent)
{
    const qsizetype targetTextLength = stem.size() + rule.decEnd.size();
    const int targetProperStepCount = properStepCount(parent);

    return std::ranges::any_of(discovered, [&](const Form &form) {
        if (form.lastTag != rule.decTag || form.text.size() != targetTextLength)
            return false;

        // The length check above makes first()/sliced() in range. QStringView::startsWith() is
        // avoided here because it answers a null view by the nullness of the needle rather than
        // by content.
        const QStringView formText{form.text};
        if (formText.first(stem.size()) != stem || formText.sliced(stem.size()) != rule.decEnd)
            return false;

        const ProcessNode *process = form.process.get();
        return process != nullptr && process->properStepCount == targetProperStepCount &&
               process->detail == rule.detail && process->parent.get() == parent.get();
    });
}

void applyRule(const Form &form, const Rule &rule, std::vector<Form> &newForms)
{
    switch (rule.type) {
    case RuleType::OnlyFinal:
        if (!form.lastTag.isEmpty())
            return;
        break;
    case RuleType::Rewrite:
        if (form.text != rule.conEnd)
            return;
        break;
    case RuleType::NeverFinal:
        if (form.lastTag.isEmpty())
            return;
        break;
    case RuleType::Standard:
        break;
    }

    const QStringView textSpan{form.text};

    // A rule that would consume the whole text and write nothing back produces an empty form.
    if (textSpan.size() == rule.conEnd.size() && rule.decEnd.isEmpty())
        return;

    if (properStepCount(form.process) > kMaxProperSteps - 1)
        return;

    // Written as an explicit suffix comparison rather than QStringView::endsWith(): that
    // function reports a null view as ending only on a null needle, so a null QString input
    // would reject every rule with an empty con_end, which C#'s span EndsWith accepts.
    if (rule.conEnd.size() > textSpan.size() || textSpan.last(rule.conEnd.size()) != rule.conEnd)
        return;

    const QStringView stem = textSpan.first(textSpan.size() - rule.conEnd.size());
    if (discoveredFormsContain(newForms, stem, rule, form.process))
        return;

    QString newText;
    newText.reserve(stem.size() + rule.decEnd.size());
    newText.append(stem);
    newText.append(rule.decEnd);

    const int parentSteps = properStepCount(form.process);
    const bool properStep = !rule.detail.isEmpty() && rule.detail.front() != u'(';
    const int steps = form.process ? parentSteps + (properStep ? 1 : 0) : 1;

    newForms.push_back(Form{.text = std::move(newText),
                            .originalText = form.originalText,
                            .lastTag = rule.decTag,
                            .process = std::make_shared<const ProcessNode>(form.process, rule.detail, steps)});
}

void applyBucket(const Form &form, const RuleBucket &bucket, std::vector<Form> &newForms)
{
    // The seed form carries no tag, so every rule of the bucket may consume it. A later form
    // is consumed only by a rule whose conTag is the tag the previous rule produced, which is
    // what turns the rule table into a finite automaton over stem tags.
    if (!form.process) {
        for (const Rule &rule : bucket.allRules)
            applyRule(form, rule, newForms);
        return;
    }

    const auto it = bucket.rulesByConTag.find(form.lastTag);
    if (it == bucket.rulesByConTag.end())
        return;
    for (const Rule &rule : it->second)
        applyRule(form, rule, newForms);
}

} // namespace

int properStepCount(const ProcessPtr &node)
{
    return node ? node->properStepCount : 0;
}

std::vector<Form> deconjugate(const RuleSet &rules, const QString &text)
{
    return deconjugate(rules, text, nullptr);
}

std::vector<Form> deconjugate(const RuleSet &rules, const QString &text, qsizetype *untouchedPrefix)
{
    std::vector<Form> processedForms;
    std::vector<Form> formsToProcess;
    std::vector<Form> newFormsToProcess;
    formsToProcess.push_back(Form{.text = text, .originalText = text, .lastTag = QStringView(), .process = nullptr});

    // A rule removes a suffix it has just read and writes its decEnd after what is left, so a
    // character below every visited form's read window is also below every suffix a rule
    // removed, and it keeps its index in every form. The shortest visited form bounds the
    // prefix for that reason.
    qsizetype shortestForm = text.size();

    while (!formsToProcess.empty()) {
        newFormsToProcess.clear();

        for (const Form &form : formsToProcess) {
            shortestForm = std::min(shortestForm, form.text.size());
            if (!form.text.isEmpty()) {
                if (const RuleBucket *bucket = rules.bucketForLastChar(form.text.back()))
                    applyBucket(form, *bucket, newFormsToProcess);
            }
            applyBucket(form, rules.rulesWithEmptyConEnd(), newFormsToProcess);

            if (!rules.isTerminalWordClass(form.lastTag))
                continue;

            // For one (lemma, word class) only the path with the fewest proper steps survives.
            // The scan removes every strictly worse entry but stops at the first better one,
            // which is JL's order-dependent loop reproduced verbatim.
            bool add = true;
            const int formSteps = properStepCount(form.process);
            for (qsizetype i = static_cast<qsizetype>(processedForms.size()) - 1; i >= 0; --i) {
                if (processedForms[i].text != form.text || processedForms[i].lastTag != form.lastTag)
                    continue;

                const int existingSteps = properStepCount(processedForms[i].process);
                if (existingSteps < formSteps) {
                    add = false;
                    break;
                }
                if (existingSteps > formSteps) {
                    if (i != static_cast<qsizetype>(processedForms.size()) - 1)
                        processedForms[i] = std::move(processedForms.back());
                    processedForms.pop_back();
                }
            }

            if (add)
                processedForms.push_back(form);
        }

        formsToProcess.swap(newFormsToProcess);
    }

    if (untouchedPrefix != nullptr)
        *untouchedPrefix = std::max<qsizetype>(0, shortestForm - rules.longestConEnd());
    return processedForms;
}

QString formattedProcess(const ProcessPtr &node)
{
    QString rendered;
    bool added = false;

    for (const ProcessNode *current = node.get(); current != nullptr; current = current->parent.get()) {
        const QStringView detail = current->detail;
        if (detail.isEmpty())
            continue;

        QStringView step = detail;
        // A parenthesised detail holds U+0028 at index 0, U+0029 at the last index and at
        // least 2 characters. A detail of "(" or "(x" holds one parenthesis and renders
        // verbatim, which keeps the slice length at 0 or above for a hand-written rule set
        // RuleSet::loadFromJson() accepts.
        if (detail.size() >= 2 && detail.front() == u'(' && detail.back() == u')') {
            // A parenthesised detail names an intermediate stem. It stays hidden mid-chain and
            // is shown without its parentheses at the root, which is the step nearest the
            // surface form.
            if (current->parent)
                continue;
            step = detail.sliced(1, detail.size() - 2);
            // "()" carries no stem name and is skipped like an empty detail.
            if (step.isEmpty())
                continue;
        }

        if (added)
            rendered.append(u'→');
        rendered.append(step);
        added = true;
    }

    return rendered;
}

QString deconjugationProcessText(std::span<const ProcessPtr> paths)
{
    QString text;
    for (const ProcessPtr &path : paths) {
        const QString rendered = formattedProcess(path);
        if (rendered.isEmpty())
            continue;
        // U+FF5E marks the first rendered path, which is the first path whose formattedProcess()
        // is non-empty rather than the element at index 0. The lookup query path likewise
        // excludes empty renderings before assembling its displayed process list.
        text.append(text.isEmpty() ? u"～"_s : u"; "_s);
        text.append(rendered);
    }
    return text;
}

} // namespace maru::deconj

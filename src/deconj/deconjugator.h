// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Deconjugation: the breadth-first search that turns a conjugated Japanese surface form into
// the dictionary forms it can come from. Ported from JL (Apache-2.0),
// JL.Core/Deconjugation/{Deconjugator,DeconjugatorUtils,Rule,VirtualRule,Form,ProcessNode}.cs
// and JL.Core/Lookup/{RuleBucket,LookupResultUtils}.cs at commit 85ae02eeb84f378387f48c12e7b468390a9f2007. JL's
// engine is itself a rewrite of Nazeka's (Apache-2.0), and the rule set in
// data/deconjugation_rules.json is Nazeka-derived and JL-extended.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringView>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace maru::deconj
{

// The guard a rule carries, from the "type" string of the rule set. Nazeka defines two further
// types, contextrule and substitution; neither occurs in JL's rule set, and loadFromJson()
// rejects any string outside the four below rather than skipping the rule.
enum class RuleType : std::uint8_t
{
    Standard,   // "stdrule": no guard
    Rewrite,    // "rewriterule": the whole current text must equal conEnd
    OnlyFinal,  // "onlyfinalrule": applicable to the seed form alone
    NeverFinal, // "neverfinalrule": applicable to any form except the seed
};

// One virtual rule: the rule set stores parallel arrays, and load expands them into one Rule
// per index of con_end. Every view points into the owning RuleSet's arena, so a Rule is six
// pointers and a tag comparison is a length check plus a memcmp.
struct Rule
{
    RuleType type = RuleType::Standard;
    QStringView decEnd; // the lemma-ward ending this rule writes
    QStringView conEnd; // the surface ending this rule consumes
    QStringView decTag; // the tag the produced form carries
    QStringView conTag; // the tag the consumed form must carry
    QStringView detail; // the step label the rendered path shows
};

// Hashes a QStringView by content, so a bucket keyed by an interned tag can also be probed
// with a view that is not interned.
struct StringViewHash
{
    using is_transparent = void;

    [[nodiscard]] std::size_t operator()(QStringView text) const noexcept
    {
        return qHash(text);
    }
};

// The rules whose conEnd shares one last character, plus the same rules indexed by conTag. A
// form with a tag consults only rulesByConTag; the seed form, which carries no tag, consults
// allRules.
struct RuleBucket
{
    std::vector<Rule> allRules;
    std::unordered_map<QStringView, std::vector<Rule>, StringViewHash, std::equal_to<>> rulesByConTag;
};

struct ProcessNode;
using ProcessPtr = std::shared_ptr<const ProcessNode>;

// One step of a deconjugation path, as an immutable list toward the root. The root is the rule
// applied to the surface form. Sibling forms share their ancestors, which is what lets the
// intra-generation dedup compare parents by pointer identity, as JL's ProcessNode.Equals does.
struct ProcessNode
{
    ProcessPtr parent; // null on the root
    QStringView detail;
    // parent == null ? 1 : parent->properStepCount + (detail is neither empty nor parenthesised)
    int properStepCount = 1;
};

// The 8-proper-step cutoff. JL prunes a form whose parent process already holds more than 7
// proper steps, so a path never exceeds 8 of them.
inline constexpr int kMaxProperSteps = 8;

// One node of the search. text is the lemma once the form is accepted; originalText is the
// surface input, shared unchanged by every form of one search.
struct Form
{
    QString text;
    QString originalText;
    QStringView lastTag; // empty on the seed form
    ProcessPtr process;  // null on the seed form
};

// The 24 JMdict word classes a deconjugation may end on. A form whose lastTag is outside this
// set is an intermediate stem and is never returned.
[[nodiscard]] bool isTerminalWordClass(QStringView tag);

// The 24 terminal word classes, sorted, for tests and for the dictionary word-class gate.
[[nodiscard]] std::span<const QStringView> terminalWordClasses();

// The expanded rule set. Immutable after load, so one instance is shared by every lookup
// thread. Move-only: the interned strings live in one QString whose views the rules hold.
class RuleSet
{
public:
    RuleSet();
    ~RuleSet();
    RuleSet(const RuleSet &) = delete;
    RuleSet &operator=(const RuleSet &) = delete;
    RuleSet(RuleSet &&) noexcept;
    RuleSet &operator=(RuleSet &&) noexcept;

    // Parses the rule set. Returns nullopt and, when errorString is non-null, a one-line reason
    // on: malformed JSON, a top level that is not an array, a rule that is not an object, a
    // missing or mistyped field, an unknown type string, dec_end and con_end of different
    // lengths, or a tag array whose length is neither 1 nor con_end's length.
    [[nodiscard]] static std::optional<RuleSet> loadFromJson(const QByteArray &json, QString *errorString = nullptr);

    // Parses the copy compiled into the binary at :/marupop/deconjugation_rules.json.
    [[nodiscard]] static std::optional<RuleSet> loadEmbedded(QString *errorString = nullptr);

    // The bucket of rules whose conEnd ends on lastCharacter, or nullptr when no rule does.
    [[nodiscard]] const RuleBucket *bucketForLastChar(QChar lastCharacter) const;

    // The rules with an empty conEnd, which apply to every form regardless of its text.
    [[nodiscard]] const RuleBucket &rulesWithEmptyConEnd() const;

    [[nodiscard]] bool isTerminalWordClass(QStringView tag) const;

    // The rule objects the JSON held, before virtual-rule expansion.
    [[nodiscard]] int ruleCount() const;

    // The longest conEnd of any rule, in UTF-16 code units: the most of a form's text, counted
    // from its end, that any rule reads. A rewrite rule reads the whole text, which is never
    // longer than its conEnd where it matches.
    [[nodiscard]] qsizetype longestConEnd() const;

    // Every virtual rule, in file order. Kept for the rule-set tests and for the data-sanity
    // check that no intermediate tag is a dead end.
    [[nodiscard]] std::span<const Rule> virtualRules() const;

private:
    struct Private;
    std::unique_ptr<Private> m_data;
};

// Every dictionary form the surface text can be a conjugation of. text is expected to be the
// normalized hiragana key maru::jp::normalizeText() produces. Pure and thread-safe: it reads
// rules and allocates only its own result.
[[nodiscard]] std::vector<Form> deconjugate(const RuleSet &rules, const QString &text);

// The same search, also reporting in untouchedPrefix how many leading code units of text no rule
// read on any form the search visited. Every rule compares a suffix of at most
// RuleSet::longestConEnd() code units, so the prefix is the shortest visited form less that
// length. Within it the search does not depend on the text at all: text with one of those
// characters replaced reaches the same forms with the same character replaced, which is what the
// recognition-variant pass of lookup/ocrvariants.h derives its forms from.
[[nodiscard]] std::vector<Form> deconjugate(const RuleSet &rules, const QString &text, qsizetype *untouchedPrefix);

// The number of proper steps a path holds, 0 for a null path.
[[nodiscard]] int properStepCount(const ProcessPtr &node);

// One path rendered leaf to root, joined by U+2192: "causative→passive→negative→past". An
// empty detail is skipped. A parenthesised detail, meaning one of at least 2 characters
// holding U+0028 at index 0 and U+0029 at the last index, is skipped at every node holding a
// parent and is rendered without its parentheses at the root; "()" is skipped like an empty
// detail. A detail holding one parenthesis renders verbatim. Returns an empty string when the
// path renders to nothing.
[[nodiscard]] QString formattedProcess(const ProcessPtr &node);

// Several paths for one dictionary record, as the popup shows them: "～a→b; c→d". A path
// rendering to an empty string is dropped, and the first path that renders carries the U+FF5E
// prefix. Returns an empty string when no path renders.
[[nodiscard]] QString deconjugationProcessText(std::span<const ProcessPtr> paths);

} // namespace maru::deconj

<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Style

Use an impersonal, analytic, and objective tone.

## User interface

Follow the [KDE text and label guidelines](https://develop.kde.org/hig/text_and_labels/). Use concise labels and short, factual sentences.

Apply the documentation rules below to interface explanations:

- Use title case for window titles, page titles, group titles, buttons, and menu actions.
- Use sentence case for control labels, checkbox labels, choices, tooltips, and status messages.
- Start action labels with a verb. Use descriptors for values and states.
- Use ellipses for actions requiring further input. Omit ellipses from placeholders and submenu labels.
- Put control behavior and conditions in tooltips. Keep implementation details in diagnostics.
- State the failed operation in errors. Include a recovery action when one is available.
- Use "Delete" for actions that delete files. State which data is deleted in the confirmation.
- Give icon-only controls an accessible name describing the action.
- Keep displayed names separate from stored setting keys, enum names, and dictionary format values.
- Use translation functions for application messages. Regenerate `po/marupop.pot` after changing strings.

Use the following terminology consistently:

| Concept | Term |
| --- | --- |
| Dictionary result window near the mouse | Popup |
| Input location | Mouse pointer; pointer in labels |
| Recognition process | Text recognition |
| Dictionary search | Lookup as a noun; look up as a verb |
| Dictionary entry spelling | Headword |
| Pronunciation text | Reading |
| Inflection analysis | Deconjugation |
| Pronunciation pitch | Pitch accent |
| Alternate characters searched after recognition | Character variants |
| Numeric limits | Minimum, maximum, unlimited |
| Filesystem container | Folder |

## Documentation

Documentation (comments and markdown) must be concise, factual, and unambiguous:

### Sentences

- Never write a sentence that expresses an emotion, a complaint, or a mistake.
- Never write a sentence using deictic expressions.
- Never write a sentence that is a metaphor or analogy.
- Never write a sentence rephrasing adjacent code.

### Words

- Use exact (technical) terms for concepts.
- State the target of every reference.
- Give every quantity a number and a unit.
- Give every dimension a number and a unit.
- Back every adjective with a verifiable value.
- Keep every adjective next to the noun it qualifies.
- Keep multi-word terms to three words.
- Drop a modifier that restates its noun.
- Drop the article before a target that has an identifier.
- Never use a placeholder noun over its exact term.

### Claims

- State all qualifiers after a main claim.
- State subject properties explicitly.
- State each outcome by an observable.
- State the criterion that defines a set before naming its members.
- Never write more than one claim per sentence.
- Never define a subject implicitly through the absence or lack of a specific property.
- Never write a negated claim.

### Comparisons

- State all properties explicitly of the compared concept.
- Never write a comparison without stating the compared properties.
- Never write a negative contrastive comparison.

### Measurements

- Every measurement requires a unit.
- Every measurement requires a runnable command, input description and environment.

### Time references

- State technical behavior with conditions and named tests.
- Preserve public source revisions used for provenance and reproducibility.
- Keep private investigation dates, unpublished commit references and workspace paths out of maintained documentation.

### Procedures

- Put one action in each step.
- Put the expected result in the same step as its action.
- Put every value that a step produces in that step.
- Never use an imperative in a callout.

### Lists

- End the lead-in with a colon.
- Put a period on an item that is a full sentence.
- State a fact or direction in the lead-in.
- Keep one type per list: all actions, or all items.
- Break a coordination of more than three members into a vertical list.
- Repeat a negation for each negated item.

### Layout

- Put the section conclusion in the first sentence.
- Use a table where every row contains the same fields.
- Never write a complete sentence in a heading.

### Exact text

Use exact text and references for the elements below:

- Code blocks and inline identifiers
- Function, type, and macro names
- Compiler flags, linker flags, and file paths
- Environment variable names
- Error strings, status codes, and log lines
- Register names and struct field offsets.

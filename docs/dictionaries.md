# Optional dictionaries

Ordinary vocabulary uses Rime's `*.dict.yaml` files and `import_tables`. It is
**disabled by default** in every base scheme. The adapter uses librime's own
source reader, then imports full-pinyin entries into WeType's ordinary native
user dictionary. WeType supplies matching, ranking, composition and learning;
there is no second Rime translator for these entries.

## Enable or disable

Installation supplies two empty starter files in the Rime user directory:

- `wetype.dict.yaml`: the main dictionary and its `import_tables` list.
- `wetype_extra.dict.yaml`: a personal vocabulary table with commented examples.

Existing dictionary files are preserved during installation and `sync-config`.
Once installed, edit them directly in the Rime user directory. The checked-in
files are starter templates, not replacements for personal vocabulary.
Add at least one entry before enabling; Rime cannot compile an empty dictionary.

To enable the dictionary for the unified scheme, merge this into
`wetype.custom.yaml`:

```yaml
patch:
  translator/dictionary: wetype
```

For an independent scheme, use its usual patch filename, for example
`wetype_flypy.custom.yaml`. The optional patch is also supplied in
[`examples/rime-extensions/dictionaries/`](../examples/rime-extensions/dictionaries/wetype.custom.yaml).
Finish or cancel the current composition, then deploy in Squirrel.

To disable it again, change the setting and deploy:

```yaml
patch:
  translator/dictionary: null
```

A null value removes the setting and disables imports. An empty string is not a valid main dictionary name for Rime deployment. Do not delete the native learning directory.
A disabled pinyin scheme removes the previously active dictionary's unlearned
imports when its engine is next used. Other pinyin schemes use their own
`translator/dictionary` setting; their active imports replace the previous set.
Native learning remains shared between pinyin layouts and schemes.

## Add vocabulary and domain tables

The entry columns are text, full pinyin separated by spaces, and optional initial
weight. Separate columns with literal Tab characters:

```text
细胞词库	xi bao ci ku	100
专业术语	zhuan ye shu yu	200
```

Use full pinyin even for double-pinyin schemes: the native engine handles the
layout. These imports are for pinyin modes; Wubi modes do not import them.
Codes are lowercase ASCII syllables, using `v` where appropriate for the engine.

To keep domains separate, create additional ordinary dictionaries, such as
`medical.dict.yaml` and `games.dict.yaml`, with matching `name` headers. List
them in `wetype.dict.yaml`:

```yaml
---
name: wetype
version: "1.0"
sort: by_weight
use_preset_vocabulary: false
import_tables:
  - wetype_extra
  - medical
  # - games
...
```

Comment or uncomment a list item and deploy to exclude or include that table.
The main file can also contain entries after `...`. Import lists follow Rime's
normal direct-table semantics; imported files' own import lists are not expanded
recursively. Rime's `columns` setting and source-reader phrase encoding are
supported. Entries without codes require enough character entries for Rime to
encode them; explicit full-pinyin codes are the most predictable choice for
small personal lists.

## Weights and learning

`weight` supplies an initial native ranking bias, not a fixed candidate position.
The two engines use different score scales. The adapter maps a non-negative Rime
weight to a bounded logarithmic native bias: one hour per `log2(1 + weight)` in
the engine's time-domain score, capped at fourteen days. This is an adapter
mapping, not equivalence to Rime's ranking formula. Weights from 0 through
1,000,000,000,000 are accepted; native floating-point precision can merge small
differences. If multiple tables contain the same text and code, the largest
weight is used.

Unchanged deployment does not reapply weights or inflate learning. Changing the
weight of an unlearned import replaces its initial native record. Existing
native learning is preserved and is not overwritten by a source weight.

Disabling a table removes its unlearned imported records. Selecting an imported
word retains it as native learning, so it may remain available after disabling
its source, as with Rime's static dictionary and user database. Source edits do
not clear native candidate-deletion history. The local import ledger is stored
alongside the native learning data; back up the runtime's whole `state/`
directory. It is not a Rime `.userdb`.

Ordinary imported words follow the scheme's usual filters, including OpenCC.
`custom_phrase` remains a separate shortcut configuration; this interface does
not turn dictionary words into literal, pinned shortcuts.

## Limits and local diagnostics

The interface accepts up to 10,000 collected entries, 128 bytes per encoded
pinyin code, 4,096 bytes per text, 2 MiB of combined code/text data and 16 MiB of
source files. Imports run during background preparation rather than per key.
Enabling or changing a large dictionary can delay the first query whilst import
finishes.

Invalid configuration leaves ordinary native input available and sets
`_wetype_dictionary_error`. `_wetype_dictionary_entries` reports the collected
entry count. The private native import ABI is optional: a missing interface
prevents dictionary import whilst ordinary typing continues. It has been tested
with the macOS 3.0.1 (680) engine.

This feature does not enable WeType's bundled binary cell dictionaries or
network updates. Dictionary sources, the native import ledger and learning
remain local. Workers retain the network-denied policy.

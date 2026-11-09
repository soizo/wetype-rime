# User words and custom phrases

Configure phrases in the scheme's ordinary `custom_phrase` section, and make personal changes in `<schema_id>.custom.yaml`. The text table uses Rime's existing format. The adapter reads it with librime's official `stabledb` component and sends the entries to WeType's native user-word store. Candidates and selection learning come from WeType; exact custom-phrase matches receive the configured priority.

## Configuration

The basic schemes contain:

```yaml
custom_phrase:
  dictionary: ""
  user_dict: custom_phrase
  db_class: stabledb
  initial_quality: 99
```

This reads `custom_phrase.txt` from the Rime user directory (`~/Library/Rime/` on Squirrel). An existing file can be reused without conversion. The adapter only reads the file; it does not rewrite it. There is no separate shared user-word YAML file to maintain.

To choose another table, use a normal Rime patch, for example:

```yaml
# wetype_flypy.custom.yaml
patch:
  custom_phrase/user_dict: custom_phrase_double
```

That scheme reads `custom_phrase_double.txt`. Table names are arbitrary: set `user_dict` to the filename without `.txt`. For example, an existing `wetype_user_words.txt` continues to work with `custom_phrase/user_dict: wetype_user_words`.

Use UTF-8 text with a literal Tab between fields:

```text
引擎用户词	wtrime	10
自定义短语	zi ding yi duan yu
```

The columns are phrase, input code and optional weight. Lines beginning with `#` are comments. Codes represent literal keys; spaces between syllables are removed before native import. A double-pinyin shortcut should use that layout's keys. Negative weights exclude a row. Exact custom phrases returned by the engine appear before ordinary and fuzzy candidates, with larger row weights first and native ordering for ties. Prefix matches retain their native order.

`custom_phrase/initial_quality` defaults to `99` and also supplies the promoted candidates' Rime quality. Override it in the scheme's `.custom.yaml` as usual. Set it to `0` or below to retain native ordering. Reordering preserves the engine's original selection identifiers, so number keys, selection learning and candidate deletion still reach the correct native word.

Edit the table or the scheme's `.custom.yaml`, then use Squirrel's **Deploy** command. Changes made directly in the Rime user directory need no project command. Finish or cancel an existing composition first. A [commented template](../examples/rime-extensions/user-words/custom_phrase.txt) is supplied for new files; do not replace an existing personal table with it.

If you prefer to keep configuration in the checkout, `python3 -m wetype sync-config` seeds missing personal files from `data/` before deployment. Existing installed personal patches and text tables are preserved; edit those files directly after their first copy. Personal text tables in that directory are ignored by Git. Destination-only personal files are preserved.

## Literal output and OpenCC

Configured native phrases use Rime's standard `user_table` candidate type, independently of their priority. The supplied mainland scheme and traditional-output example exclude that type from OpenCC, so custom phrases retain exactly the text in the table whilst ordinary engine candidates follow the selected script conversion. This also works with `initial_quality: 0` and with the optional official phrase translator.

When adding another Rime `simplifier`, give each conversion namespace the usual exclusion, for example:

```yaml
traditionalize:
  option_name: traditionalization
  opencc_config: s2t.json
  excluded_types: [user_table]
```

Selection learning and deletion continue to use the original native candidate identifiers.

## Optional Rime phrase translator

The usual `table_translator@custom_phrase` remains available. Adding it to `engine/translators` hands that namespace to the official Rime translator, including its `initial_quality`, completion and sentence options. The adapter then stops importing that table into the native store, so it is not processed by two backends. See the [phrase example](../examples/rime-extensions/phrases/wetype.custom.yaml).

The native path honours `initial_quality` and exact-match row weights. Completion and sentence-generation settings belong to the optional official table translator. Ordinary WeType candidates and their learning continue through the native engine when that translator is used.

## Removal and learning

Delete a configured row and deploy to remove its imported native record. Reconciliation affects that table's records; it preserves other tables and ordinary native learning. To clear a table's imported records whilst keeping the source, patch `custom_phrase/enable_user_dict: false` and deploy. This setting applies to the phrase table, not the native main dictionary. Keep `user_dict` unchanged so its records can be identified. An empty dictionary name, or a renamed table, leaves the previous source's native records in place.

Highlight a candidate and use Rime's standard **Control + Delete** action to request native deletion. On Mac keyboards where Delete sends Backspace, use **Fn + Control + Delete**. Exact phrase/code matches are removed from the native hot-word store; other candidates use WeType's learned-candidate deletion interface. Bundled dictionary words may remain available.

Candidate deletion does not rewrite the text table. A configured phrase can be imported again after reinitialisation, so remove its source row for a lasting configuration change.

All pinyin layouts share native learning and hot-word storage; each Wubi variant has a separate store. Codes are not automatically converted between layouts. Rime's `.userdb` management tools do not edit WeType's binary learning data. Back up the runtime's `state/` directory as well as your text tables if you need both.

## Candidate pinning and blocking

Edit `wetype-candidate-rules.custom.yaml` in your Rime user directory. A new installation starts with an empty rules list. Rime loads changes on the next candidate query. If the file contains invalid YAML or an invalid rule, the running input method keeps the last valid rules and reports the error in `_wetype_rules_error`.

```yaml
- layout: flypy
  code: ni
  pin: [你, 尼]
  block: [拟]
```

- `layout` is the WeType mode ID, such as `quanpin`, `flypy` or `wubi86`. Unified and standalone schemes share rules for the same layout.
- `code` is the literal input covered by the candidate. For partial candidates it is only the covered part of the input. Match the original candidate text before any OpenCC conversion.
- `pin` lists existing candidates in the order they should appear. `block` hides matches. If a word is in both lists, it stays hidden until the block is removed.
- Rules apply only to candidates returned by WeType. They preserve the original selection, partial selection and native learning. Rime table-translator candidates retain their own ordering and behaviour.

Multiple entries for the same layout and code are applied in file order. Later entries can remove particular pins with `unpin: [word]` or clear blocks for that code with `restore: true`. You can also remove rules directly from the file. A rule can only reorder or hide candidates that the engine returns.

### Optional shortcuts

All four shortcuts default to disabled. For an individual scheme, add these opt-in bindings to its `.custom.yaml` and deploy.

```yaml
patch:
  wetype/candidate_keys/pin: Control+p
  wetype/candidate_keys/block: Control+x
```

The same keys can be set for all supplied WeType schemes under `settings/candidate_keys/` in `wetype_common.custom.yaml`. The available actions are `pin` and `block` for the highlighted candidate, `unpin` for the highlighted candidate, and `restore` for all blocked candidates under the full current code. Use an empty string to disable a binding.

Shortcuts append entries to the same YAML file without reformatting its comments or existing rules. A new pin goes after the existing pins for that code. Use a single block-style list without YAML document markers to permit writes. The installed comment-only template is ready to use. Stop editing the file before invoking a shortcut, then reload it in your editor before editing again. The file limit is 256 KiB.

Candidate display rules do not change native learning. `Control + Delete` remains the native learned-entry removal action. The old native preference files are no longer read or migrated.

## Limits and diagnostics

Native imports accept up to 2,048 active entries, 128 bytes per code, 4,096 bytes per phrase and 2 MiB of combined phrase/code data. Unsupported configuration or rejected imports leave ordinary input available and set `_wetype_user_words_error`; deletion failures set `_wetype_delete_error`.

Native input codes use ASCII letters, apostrophes and, in the relevant layouts, semicolons. Rows with other code characters or codes exceeding 128 bytes are skipped individually, so they do not disable valid phrases or their priority after deployment. Their source rows are preserved. `_wetype_user_words_skipped` and a count-only local log entry report how many codes were skipped; negative-weight rows are intentionally excluded and are not counted as incompatible.

The adapter does not log table contents. Vendor diagnostics can contain words and remain local `.log` files excluded from Git. Workers retain the network-denied policy and run independently of the official WeType application.

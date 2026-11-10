# User words and custom phrases

All supplied WeType schemes use Rime's standard `table_translator@custom_phrase` for personal shortcuts. A phrase can have several input codes, and Rime looks up each code independently. Ordinary WeType candidates still use the native engine and its learning.

## Configuration

Put your shortcuts in `custom_phrase.txt` in the Rime user directory (`~/Library/Rime/` on Squirrel). Use UTF-8 and separate the phrase, input code and optional weight with literal tabs.

```text
ニコニコ	niconico	4
ﾆｺﾆｺ	niconico	3
ニコニコ	nikoniko	4
ﾆｺﾆｺ	nikoniko	3
```

Use the literal keys for your scheme, without spaces between syllables. Both codes above return the full-width phrase before the half-width phrase. The defaults require the complete code and disable sentence construction from the phrase table. Rime uses `custom_phrase/initial_quality: 99` to rank the table against other translators.

To choose another table for one scheme, patch its `custom_phrase/user_dict`. For example, add this to `wetype_flypy.custom.yaml` to read `custom_phrase_double.txt`.

```yaml
patch:
  custom_phrase/user_dict: custom_phrase_double
```

For a shared change, use `base/custom_phrase/user_dict` in `wetype_common.custom.yaml`. The name is the filename without `.txt`. To disable the table for a scheme, set `custom_phrase/enable_user_dict: false`.

Edit the table or patch, then use Squirrel's **Deploy** command. To remove a shortcut, delete its row and deploy. A [commented template](../examples/rime-extensions/user-words/custom_phrase.txt) is available for new tables.

When upgrading from an earlier version, run `python3 -m wetype sync-config`. Remove any personal patch entry that adds `table_translator@custom_phrase`, as the shared preset now supplies it. Keep personal phrase-table settings and files in the Rime user directory, then deploy.

## Literal output and OpenCC

Rime assigns exact phrase candidates the `user_table` type. The mainland scheme and traditional-output example exclude that type from OpenCC, preserving the text in the table whilst ordinary candidates follow the selected script conversion. When adding a Rime `simplifier`, set the exclusion on each conversion namespace.

```yaml
traditionalize:
  option_name: traditionalization
  opencc_config: s2t.json
  excluded_types: [user_table]
```

## Legacy WeType phrase import

The adapter can still import a text table into WeType's native user-word store if a personal patch supplies a translator list without `table_translator@custom_phrase`. Use a separate table name for an opt-in scheme. Keep any expression translators that the scheme uses.

```yaml
patch:
  engine/translators: [punct_translator, wetype_translator]
  custom_phrase/user_dict: wetype_native_phrases
```

The native path only promotes matches returned by WeType. It strips spaces in codes before import, skips negative weights and uses native ordering for ties. A zero or negative `initial_quality` retains native ordering. The legacy import limits and diagnostics are listed below.

On upgrade, schemes using Rime's translator remove previous WeType imports owned by the configured table. Ordinary WeType learning and native words owned by other sources are preserved. To clear a legacy import before changing its table name, disable its user dictionary and deploy with the old name still configured.

To delete a native candidate, highlight it and press **Control + Delete**. On Mac keyboards where Delete sends Backspace, press **Fn + Control + Delete**. This removes a matching native hot word or learned candidate. The text table remains the source for reimport, so remove its row and deploy to delete the shortcut permanently.

The pinyin layouts share native learning and hot-word storage. Each Wubi variant has a separate store. Back up the runtime's `state/` directory to preserve native learning alongside the text tables.

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

## Native import limits and diagnostics

Native imports accept up to 2,048 active entries, 128 bytes per code, 4,096 bytes per phrase and 2 MiB of combined phrase/code data. Unsupported configuration or rejected imports leave ordinary input available and set `_wetype_user_words_error`; deletion failures set `_wetype_delete_error`.

Native input codes use ASCII letters, apostrophes and, in the relevant layouts, semicolons. Rows with other code characters or codes exceeding 128 bytes are skipped individually, so they do not disable valid phrases or their priority after deployment. Their source rows are preserved. `_wetype_user_words_skipped` and a count-only local log entry report how many codes were skipped; negative-weight rows are intentionally excluded and are not counted as incompatible.

The adapter does not log table contents. Vendor diagnostics can contain words and remain local `.log` files excluded from Git. Workers retain the network-denied policy and run independently of the official WeType application.

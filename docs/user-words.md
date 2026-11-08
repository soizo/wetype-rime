# User words and custom phrases

Configure phrases in the scheme's ordinary `custom_phrase` section, and make personal changes in `<schema_id>.custom.yaml`. The text table uses Rime's existing format. WeTypeRime reads it with librime's official `stabledb` component and sends the entries to WeType's native user-word store. Candidates, ranking and selection learning come from WeType.

## Configuration

The basic schemes contain:

```yaml
custom_phrase:
  dictionary: ""
  user_dict: custom_phrase
  db_class: stabledb
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

The columns are phrase, input code and optional weight. Lines beginning with `#` are comments. Codes represent literal keys; spaces between syllables are removed before native import. A double-pinyin shortcut should use that layout's keys. Negative weights exclude a row. Other weights are parsed by Rime, whilst WeType decides native candidate positions.

Edit the table or the scheme's `.custom.yaml`, then use Squirrel's **Deploy** command. Changes made directly in the Rime user directory need no project command. Finish or cancel an existing composition first. A [commented template](../examples/rime-extensions/user-words/custom_phrase.txt) is supplied for new files; do not replace an existing personal table with it.

If you prefer to keep configuration in the checkout, `python3 -m wetype sync-config` copies `rime/` files before deployment. Personal text tables in that directory are ignored by Git. Destination-only personal files are preserved.

## Optional Rime phrase translator

The usual `table_translator@custom_phrase` remains available. Adding it to `engine/translators` hands that namespace to the official Rime translator, including its `initial_quality`, completion and sentence options. WeTypeRime then stops importing that table into the native store, so it is not processed by two backends. See the [phrase example](../examples/rime-extensions/phrases/wetype.custom.yaml).

The native path does not claim to implement those additional table-translator options. The ordinary WeType candidates and their learning continue through the native engine when the optional Rime translator is used.

## Removal and learning

Delete a configured row and deploy to remove its imported native record. Reconciliation affects that table's records; it preserves other tables and ordinary native learning. To clear a table's imported records whilst keeping the source, patch `custom_phrase/enable_user_dict: false` and deploy. This setting applies to the phrase table, not the native main dictionary. Keep `user_dict` unchanged so its records can be identified. An empty dictionary name, or a renamed table, leaves the previous source's native records in place.

Highlight a candidate and use Rime's standard **Control + Delete** action to request native deletion. On Mac keyboards where Delete sends Backspace, use **Fn + Control + Delete**. Exact phrase/code matches are removed from the native hot-word store; other candidates use WeType's learned-candidate deletion interface. Bundled dictionary words may remain available.

Candidate deletion does not rewrite the text table. A configured phrase can be imported again after reinitialisation, so remove its source row for a lasting configuration change.

All pinyin layouts share native learning and hot-word storage; each Wubi variant has a separate store. Codes are not automatically converted between layouts. Rime's `.userdb` management tools do not edit WeType's binary learning data. Back up the runtime's `state/` directory as well as your text tables if you need both.

## Limits and diagnostics

Native imports accept up to 2,048 active entries, 128 bytes per code, 4,096 bytes per phrase and 2 MiB of combined phrase/code data. Unsupported configuration or rejected imports leave ordinary input available and set `_wetype_user_words_error`; deletion failures set `_wetype_delete_error`.

The adapter does not log table contents. Vendor diagnostics can contain words and remain local `.log` files excluded from Git. Workers retain the network-denied policy and run independently of the official WeType application.

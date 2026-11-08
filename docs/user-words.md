# User words

WeTypeRime reads a Rime text table through librime's official `stabledb` component and imports its entries into WeType's native user-word store. WeType generates, ranks and learns the candidates. No additional Rime translator or online service is involved.

## Add or edit phrases

Create `wetype_user_words.txt` in your Rime user directory (`~/Library/Rime/` on Squirrel), or copy the [commented template](../examples/rime-extensions/user-words/wetype_user_words.txt). Use UTF-8 text with a literal Tab between fields:

```text
引擎用户词	wtrime	10
自定义短语	zi ding yi duan yu
```

Each row contains a phrase, an input code and an optional Rime weight. Codes represent literal keys, including uppercase letters, apostrophes and semicolons where the selected mode supports them. Spaces between code syllables are removed. A code intended for double pinyin should contain that layout's keys. Lines beginning with `#` are comments. Negative weights exclude a row; other weights are parsed by Rime, but do not force WeType's candidate position.

Deploy Squirrel after editing. Existing compositions should be finished or cancelled first. To keep the source in your checkout, use `rime/wetype_user_words.txt` and run `python3 -m wetype sync-config` before deploying. That personal file is ignored by Git. The installer preserves a destination-only personal table.

The default configuration is shared by the basic schemes and inherited variants:

```yaml
# wetype_user_words.yaml
settings:
  enabled: true
  user_dict: wetype_user_words
  db_class: stabledb
```

A scheme can choose another table through its ordinary Rime customisation file:

```yaml
# wetype_flypy.custom.yaml
patch:
  wetype_user_words/user_dict: my_xiaohe_words
```

This reads `my_xiaohe_words.txt`. All pinyin layouts share the native pinyin store; the three Wubi variants use separate stores. Table codes are not automatically converted between layouts. Choose distinct literal codes where a shared phrase needs different shortcuts.

## Remove words

To remove a configured phrase permanently, delete its row from the text table, then deploy again. Changes reconcile only the native entries belonging to that table. Other tables and the engine's ordinary learning remain intact.

Rime's standard **Control + Delete** action is also connected to the native engine. Highlight a candidate before using the action. On Mac keyboards where the key labelled Delete sends Backspace, use **Fn + Control + Delete**. For a configured phrase entered with its exact code, this removes matching native hot-word records. For other candidates, the adapter calls WeType's native learned-candidate deletion interface; it does not promise to erase words from the bundled dictionary.

Deleting a candidate does not rewrite your text table. An unchanged configured phrase can be imported again on a subsequent frontend restart or configuration change. Remove its source row for a lasting configuration change.

To clear the default table's imported entries whilst keeping its source file, create this shared customisation and deploy:

```yaml
# wetype_user_words.custom.yaml
patch:
  settings/enabled: false
```

A scheme-specific table can instead set `wetype_user_words/enabled: false` in that scheme's customisation. Keep `user_dict` unchanged so the adapter can identify the entries to remove. Removing the whole configuration block or renaming the table leaves its previous native entries in place. Shared tables should use the same enabled setting across schemes to avoid conflicting instructions.

## Local storage and limits

The text table is your editable source. Imported entries and normal selection learning are held by the engine under the independent runtime's `state/` directory. Back up both if you need both configured phrases and learned behaviour. Rime synchronisation can carry the text table if you arrange it separately; it does not synchronise WeType's binary learning data.

Imports are bounded to 2,048 active entries, 128 bytes per code, 4,096 bytes per phrase and 2 MiB of combined phrase/code data. An unsupported configuration or rejected import leaves ordinary native input available and sets the Rime property `_wetype_user_words_error`. Candidate deletion failures set `_wetype_delete_error`. The adapter does not log table contents; vendor engine diagnostics may contain words and remain local `.log` files excluded from Git.

Workers retain the existing network-denied policy. User-word operations do not launch or require the official WeType application.

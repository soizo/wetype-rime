# Local expressions and personal tables

WeTypeRime supports two independent native local options alongside its existing
Unicode Emoji setting:

| Option | Native behaviour |
| --- | --- |
| `kaomoji` | Text faces, such as `开心(*^▽^*)` for full-pinyin `kaixin` |
| `symbol_emoji` | Text symbols, such as `☆` and `★` for `xingxing`, or `∞` for `wuqiong` |

Both new options start disabled. The existing Unicode Emoji setting keeps
its previous value. Each option rebuilds the native session and clears the
current composition when changed, so old candidates cannot be selected after a
switch.
The example uses Rime's `reset: 0` switch defaults; enable the wanted option
after reloading the scheme, or adjust those ordinary switch defaults yourself.

## Enable the options in a scheme

After installing the updated adapter, merge
[`expressions/wetype.custom.yaml`](../examples/rime-extensions/expressions/wetype.custom.yaml)
into the target scheme's usual `.custom.yaml` file, then deploy in Squirrel.
For the mainland Xiaohe scheme, use `wetype_flypy_gov.custom.yaml`. The supplied
patch adds the two options and the personal-table translators. Use Rime's
scheme options menu to turn them on or off.

Native built-in recommendations come from the local engine and its bundled
resources. Selection feedback uses the original native candidate identifiers.
Full-pinyin `kaixin` and Xiaohe `kdxb` both reach the engine's kaomoji candidate;
full-pinyin `xingxing` and Xiaohe `xkxk` can produce its star symbols.

This is independent of the native binary cell dictionary list. You do not need
to enable a cell pack to use the built-in expressions tested here.

## Add personal kaomoji or symbols

Installation supplies empty, commented starter tables in the Rime user directory:

- `kaomoji.txt` for text faces.
- `symbol_emoji.txt` for text symbols or combinations.

Existing files are preserved on installation and upgrade. Edit the installed
files directly. Each row uses ordinary Rime text-table syntax:

```text
(｡･ω･｡)ﾉ♡	mykao	100
(๑•̀ㅂ•́)و✧	mykao	50
```

For example, put those rows in `kaomoji.txt`, with literal Tab characters between
text, code and optional weight. Deploy, enable the kaomoji option, then type
`mykao`. Larger weights sort before smaller ones. Add symbols similarly in
`symbol_emoji.txt`:

```text
☆彡	mystar	100
→→→	myarrow	100
```

Codes are literal input keys, as with `custom_phrase.txt`; they are not full
pinyin converted between layouts. You can use the same shortcut in several
schemes or configure another table through `kaomoji/user_dict` or
`symbol_emoji/user_dict` in a scheme patch.

The personal tables use Rime's official `table_translator` and `stabledb`
components for loading, matching, weights and commit handling. A small adapter
wrapper only gates the translator with its configured `option_name`. These
literal rows are not imported into WeType's ordinary or hot-word dictionaries.
They have the standard `user_table` candidate type and the supplied priority of
99. The mainland scheme's existing OpenCC exclusion therefore preserves their
exact text. For another scheme with OpenCC, use its normal
`excluded_types: [user_table]` setting if literal output is required.

Editing or removing a row takes effect after deployment. Turning its expression
option off hides both the personal table and the corresponding native built-in
recommendations. `custom_phrase.txt` remains independent.

## Emoji Plus is prohibited

Emoji Plus is not implemented or exposed as an experimental feature. There is
no menu switch or supported scheme binding for it. The client and worker reject
activation masks outside 0–7, and the native configuration helper always clears
`enable_emoji_plus`, even if given stale flags. An obsolete
`wetype/emoji_plus_option` binding cannot enable it.

In the examined macOS 3.0.1 (680) resources, Plus-marked entries also depend on a
cloud dynamic-Emoji branch. That branch remains disabled. This observation does
not define the official product's marketing name. Existing Unicode Emoji,
kaomoji and symbols continue to work locally. Personal Unicode combinations can
be added to the symbol table as literal text.

## Validation and privacy

`python3 tests/smoke_expressions.py` checks native kaomoji and symbols, independent
Unicode Emoji settings, double pinyin, native selection, official personal
tables, priority, literal OpenCC output, edits, removal, deployment and live
switches. The ABI check verifies that expression flags do not modify cloud,
WeChat, AI or unrelated session fields. `_wetype_expression_flags` exposes the
active local mask for diagnosis: 1 for Unicode Emoji, 2 for kaomoji and 4 for
symbols.

Workers retain the network-denied policy and independent local resources.
Tables, native learning and `.log` diagnostics stay local. Neither the official
WeType client nor an online account is required whilst typing.

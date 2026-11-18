# Local expressions and personal tables

Unicode Emoji, kaomoji and text symbols come from external, editable tables by
default. The optional native engine recommendations and the personal
shortcut-code tables remain available and are described further below.

## External expression tables (default)

The supplied schemes ship three hand-maintained plain TSV tables in the Rime
user directory:

| Table | Contents | Option |
| --- | --- | --- |
| `wetype_emoji.tsv` | word → Unicode Emoji, such as `开心	😄` | `emoji` |
| `wetype_kaomoji.tsv` | word → text faces, such as `开心	开心(*^▽^*)` | `kaomoji` |
| `wetype_symbols.tsv` | word → text symbols, such as `星星	☆` | `symbol_emoji` |

Three Lua filters (`lua/wetype_expressions.lua`) match the simplified engine
candidate text against these tables and insert each expression right after its
word. Matching happens before any user-added OpenCC conversion, so the tables
use simplified keys. All three options start enabled through hidden switches in
every supplied scheme; patch a visible `states` list or a different `reset`
value in a scheme's `.custom.yaml` to change that.

Personal changes belong in the overlays installed beside them:

- `wetype_emoji.custom.tsv`
- `wetype_kaomoji.custom.tsv`
- `wetype_symbols.custom.tsv`

Each row is `word<TAB>expression`, one expression per line; repeat a word for
alternatives. Personal rows are merged over the supplied tables. Edits apply as
soon as the file bytes change — normally on the next keystroke, without
redeploying. Supplied tables refresh through `python3 -m wetype sync-config`
like other presets; overlays and other personal files are preserved.

The tables are ordinary source files: edit the checkout's `data/wetype_*.tsv`
to change the default for new installations, or edit the installed copies
directly. They carry no engine resources, network data or personal learning.

Candidate pinning and blocking rules apply to expressions together with their
word. If you add a `simplifier` conversion (for example the traditional-output
extension), keep expression text literal with
`excluded_types: [user_table, lua_emoji, lua_kaomoji, lua_symbols]`.

## Native engine recommendations (optional)

The native local options remain available alongside the tables:

| Option | Native behaviour |
| --- | --- |
| `kaomoji` | Text faces, such as `开心(*^▽^*)` for full-pinyin `kaixin` |
| `symbol_emoji` | Text symbols, such as `☆` and `★` for `xingxing`, or `∞` for `wuqiong` |

Each option rebuilds the native session and clears the current composition
when changed, so old candidates cannot be selected after a switch. While a
native category is active through its binding, the matching Lua table steps
aside automatically, so built-in and external recommendations never duplicate
each other. Selection feedback then uses the original native candidate
identifiers. Full-pinyin `kaixin` and Xiaohe `kdxb` both reach the engine's
kaomoji candidate; full-pinyin `xingxing` and Xiaohe `xkxk` can produce its
star symbols.

### Enable the native options in a scheme

Merge [`expressions/wetype.custom.yaml`](../examples/rime-extensions/expressions/wetype.custom.yaml)
into the target scheme's usual `.custom.yaml` file, then deploy in Squirrel.
For the mainland Xiaohe scheme, use `wetype_flypy_gov.custom.yaml`. The patch
binds the two existing hidden switches to the native engine and adds the
personal shortcut-code translators below. Native built-in recommendations come
from the local engine and its bundled resources. This is independent of the
native binary cell dictionary list.

Unicode Emoji follows the same pattern through
[`native-emoji/wetype.custom.yaml`](../examples/rime-extensions/native-emoji/wetype.custom.yaml),
which binds the existing `emoji` switch to the engine and keeps its ranking and
selection feedback.

## Personal shortcut-code tables

Installation supplies empty, commented starter tables in the Rime user
directory:

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
option off hides the personal table, the external table and the corresponding
native built-in recommendations together. `custom_phrase.txt` remains
independent.

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

`python3 tests/smoke_lua_expressions.py` checks the external tables through
real librime: matching, filter order, kaomoji comments, literal commit,
independent options, overlay hot reload, the native step-aside guard and
literal OpenCC output.

`python3 tests/smoke_expressions.py --mainland-dir /path/to/generated/package` checks native kaomoji and symbols, independent
Unicode Emoji settings, double pinyin, native selection, official personal
tables, priority, literal OpenCC output, edits, removal, deployment and live
switches. The ABI check verifies that expression flags do not modify cloud,
WeChat, AI or unrelated session fields. `_wetype_expression_flags` exposes the
active local mask for diagnosis: 1 for Unicode Emoji, 2 for kaomoji and 4 for
symbols.

Workers retain the network-denied policy and independent local resources.
Tables, native learning and `.log` diagnostics stay local. Neither the
official WeType client nor an online account is required whilst typing.

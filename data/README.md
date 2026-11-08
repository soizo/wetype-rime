# Rime configuration

Install the native component once, then configure these schemes in the Rime user directory and use Squirrel's **Deploy** command. Personal changes belong in `*.custom.yaml`. There is no separate settings application or required configuration-sync step for everyday edits.

## Files

| File | Purpose |
| --- | --- |
| `wetype.schema.yaml` | 微信输入, with all 11 input modes |
| `wetype_*.schema.yaml` | Standalone pinyin, double-pinyin and Wubi schemes |
| `wetype_common.yaml` | Shared processors, translators, keys, phrase settings and extension defaults |
| `wetype_fuzzy.yaml` | Rime spelling algebra and Xiaohe mappings |
| `wetype_flypy_gov.schema.yaml` | Optional 微信小鹤大陆 variant |
| `lua/` | Native-module loader and optional Lua extensions |
| `wetype/installation.yaml` | Generated module, launcher and diagnostic paths |
| `wetype/` | Local engine, binary dictionaries, adapter and learning data |

`wetype/` is created during installation; it is not distributed with the source. Its native learning format is distinct from Rime's LevelDB `.userdb` format and is kept in its own directory. Do not use Rime's user-dictionary tools on it. Editable text dictionaries and phrase tables keep the ordinary `*.dict.yaml` and `*.txt` formats in the user directory. [Rime's user-data guide](https://github.com/rime/home/wiki/UserData) describes the standard files.

## Personal changes

For example, put this in `wetype_flypy.custom.yaml` in the Rime user directory:

```yaml
patch:
  menu/page_size: 6
  custom_phrase/user_dict: custom_phrase
```

Then deploy. Existing schema IDs, option names, custom patches, OpenCC resources and personal tables remain compatible. The supplied schemes inherit Rime's key-binding and punctuation presets, retain uppercase input for the native engine, and use `custom_phrase.txt` through the ordinary `custom_phrase` namespace. See [user words](../docs/user-words.md) for priorities, literal output, deletion and optional official phrase-translator ownership.

To change a default for every supplied scheme, use `wetype_common.custom.yaml`:

```yaml
patch:
  base/menu/page_size: 6
  settings/candidate_keys/pin: Control+Shift+p
```

A scheme's own patch takes precedence over shared defaults. The unified scheme's patch remains `wetype.custom.yaml`; it does not affect the standalone schemes. Do not edit the generated `build/` files or `wetype/installation.yaml` for personal settings.

## Scheme menu

Installation adds only the unified `wetype` entry on a new profile. All standalone files are installed, and existing menu choices are retained. Register selected schemes at installation with repeatable `--schema` arguments, or edit `schema_list` in `default.custom.yaml` as usual. For example, add `- schema: wetype_flypy` or `- schema: wetype_flypy_gov` to the existing list, then deploy.

The mainland variant inherits Xiaohe and adds simplified/traditional switching, mainland traditional glyphs, native Emoji recommendations, fuzzy spelling and personal Shift-key punctuation. It requires the user's existing OpenCC `s2t.json`, `govrime_t2gov_keep_simp.json` and their dictionaries. Left Shift changes script; right Shift retains the mainland punctuation behaviour. Both OpenCC stages exclude `user_table`, preserving custom phrases literally. It uses native Xiaohe candidates and learning; it has no dependency on Rime Ice's translators or dictionaries.

## Updating supplied configuration

The checkout's `data/` directory contains the distribution presets. After changing these presets, run:

```sh
python3 -m wetype sync-config
```

Then deploy. `--rime-dir /path/to/Rime` selects another user directory. Synchronisation copies top-level YAML and TXT files and Lua files recursively. It backs up conflicting distribution presets under the checkout's `.local/backups/config-*`, preserves existing personal `*.custom.yaml`, `*.dict.yaml` and `*.txt` files, and leaves destination-only files alone. Edit personal files directly in the Rime user directory after their first installation. The optional `examples/` directory is not synchronised automatically.

Changing YAML or a personal text table does not rebuild the component or recopy the engine. Updating native functionality requires `install-rime`. That command also relocates the legacy default application-support runtime into the default Rime profile without copying live learning files, retaining a compatibility link for processes still holding old paths. Explicit external `--runtime` directories remain supported.

## Optional features

The [examples](../examples/rime-extensions/README.md) use ordinary Rime patches for phrases, exact-input filters, script conversion, expressions and dictionaries. Keep the native loader, `wetype_speller` and `wetype_translator` in the processing chain.

Native Emoji recommendations bind the Rime option named by `wetype/emoji_option`. No OpenCC Emoji map is needed. The mainland variant enables this by default; the 12 base schemes do not bind an Emoji switch. Changing the option clears composition and opens a new native session, retaining native ranking and selection feedback.

[Local expressions](../docs/expressions.md) add independent kaomoji and symbol options, enabled by default in the expression patch. Edit `kaomoji.txt` and `symbol_emoji.txt` with ordinary Rime text-table rows. Personal expressions use the official table backend and retain `user_table` literal-output exclusions. Emoji Plus remains prohibited and cannot be enabled through configuration.

[Optional dictionaries](../docs/dictionaries.md) use `translator/dictionary`, ordinary `*.dict.yaml` files and `import_tables`. The setting defaults to `null`. Enable the empty `wetype.dict.yaml` and `custom.dict.yaml` starters with `translator/dictionary: wetype`, add full-pinyin entries and deploy. WeType handles matching, initial weights and learning. Disable imports with `null`; existing dictionary files are preserved on upgrade.

[Native cell dictionaries](../docs/native-cell-dictionaries.md) are separate binary resources selected through `wetype/cell_dicts`. All base schemes use `[]`. The validated engine supplies `cell_dict.sc`, `cell_dict.ssrc` and `cell_dict.hf`; select only the required identifiers in the scheme patch and deploy. The native engine loads and ranks them locally.

## Rime fuzzy spelling

`wetype_fuzzy.yaml` contains spelling rules, canonical syllable names and Xiaohe projections. Its initial rules derive `in / ing` in both directions; commented examples cover other pairs. The syllable list contains no word dictionary. For example:

```yaml
algebra:
  - derive/in$/ing/
  - derive/ing$/in/
```

The mainland variant offers 精准音 / 模糊音 and defaults to 精准音 (fuzzy spelling off). The other base schemes can opt in using the [fuzzy patch](../examples/rime-extensions/fuzzy/wetype.custom.yaml). Full pinyin `qin` and Xiaohe `qb` can then include `qing` candidates such as 请 with the comment 模糊. Librime performs the spelling algebra; WeType's fuzzy flags stay disabled.

The original first three candidates remain ahead of deduplicated fuzzy results. Branches retain their native session, candidate identifiers and selection feedback. Fuzzy additions require complete lowercase syllables, at most 32 input characters, two fuzzy syllables per path and three expanded spellings. Branch candidates cover the full input; the original path retains partial selection. Uppercase, unsupported layouts, unrecognised syllables and longer input continue on the native path. Changing the option clears composition. Subsequent rule edits need only deployment.

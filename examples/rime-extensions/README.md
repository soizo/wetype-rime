# Optional Rime extensions

These examples are not enabled automatically. Install the native component first, then merge the required settings into the target scheme's `*.custom.yaml` in the Rime user directory and deploy. Copy accompanying text tables and Lua files to that directory's root and `lua/` respectively. Personal settings do not need a project synchronisation command.

## Custom phrases

Merge `phrases/wetype.custom.yaml` into `wetype.custom.yaml` and copy `phrases/wetype_phrases.txt` to the user directory. The example uses Rime's official table translator: `myphrase` yields 我的自定义短语, whilst other input continues through WeType. This does not replace the native binary dictionaries or learning. For phrases managed through native WeType storage instead, use the ordinary `custom_phrase.txt` configuration described in [user words](../../docs/user-words.md).

## Native Emoji

Merge `native-emoji/wetype.custom.yaml` into the scheme patch. The Emoji option starts enabled. Queries such as `xiaolian` and `haha` obtain native Emoji candidates with their original ranking and feedback; use layout-specific codes such as Xiaohe `xnlm` in double pinyin. Option changes clear composition and recreate the native session. No OpenCC Emoji map, cloud service or WeChat-specific internal expression encoding is used. 微信小鹤大陆 already includes this feature.

## Exact-input replacements

Merge `force/wetype.custom.yaml`, copy `force/wetype_force.lua` into `lua/` and edit its mappings. For the complete input `myname`, only 自定义姓名 is shown; Space commits it through the normal selection flow. Other candidates pass through unchanged. The filter does not simulate keys or commit text directly.

## Fuzzy spelling

Merge `fuzzy/wetype.custom.yaml` into the target patch; for standalone Xiaohe, use `wetype_flypy.custom.yaml`. Rules live in `wetype_fuzzy.yaml` and use Rime's `derive` algebra. The initial `in / ing` pair works in both directions. Full pinyin `qin` and Xiaohe `qb` can include `qing` results such as 请 after the first three native candidates. Native engine fuzzy options remain disabled. 微信小鹤大陆 already includes the switch. See [configuration](../../data/README.md#rime-fuzzy-spelling) for supported inputs and bounds.

## Script conversion

Merge `traditional/wetype.custom.yaml` into the scheme patch. The 简体 / 繁體 option defaults to simplified output and uses OpenCC `s2t.json` when enabled. Squirrel must provide its resources. Conversion changes displayed candidates whilst feedback retains the original native candidate identity. Rime's standard `excluded_types: [user_table]` preserves custom phrases literally for both native and official phrase translators.

## Combining examples

Each `wetype.custom.yaml` is a separate example. Merge the desired entries into one `patch:` mapping, combining list additions and avoiding duplicate keys. Place the exact-input filter before script conversion if its fixed text should also be convertible. To target another scheme, use its schema ID as the patch filename, for example `wetype_quanpin.custom.yaml`; text tables and Lua files can be shared.

The checkout's `data/` folder can seed new files with `python3 -m wetype sync-config`, but existing personal patches and tables are preserved. Edit installed personal files directly after their first copy. Rime's [configuration syntax](https://github.com/rime/home/wiki/Configuration) and [customisation guide](https://github.com/rime/home/wiki/CustomizationGuide) apply.

## Optional dictionaries

Merge `dictionaries/wetype.custom.yaml` to enable `wetype.dict.yaml`. Its `import_tables` list selects ordinary Rime dictionaries, including the empty `custom.dict.yaml` starter. Set `translator/dictionary: null` and deploy to disable imports. This feature is disabled by default. See [dictionaries](../../docs/dictionaries.md) for entry formats, native weights and retained learning.

## Native cell dictionaries

Merge `native-cell-dictionaries/wetype.custom.yaml`. It selects the three native binary packs supplied by the validated engine. Keep only the required identifiers, or set `wetype/cell_dicts: []` to disable them. These packs are disabled by default and independent of ordinary Rime vocabulary imports. See [native cell dictionaries](../../docs/native-cell-dictionaries.md).

## Local expressions

Merge `expressions/wetype.custom.yaml` for independent kaomoji and symbol options, initially disabled. Installation supplies `kaomoji.txt` and `symbol_emoji.txt` starters without replacing existing files. Add literal expressions as ordinary Rime text-table rows and deploy. Native recommendations use WeType; personal rows use Rime's official table translator. See [expressions](../../docs/expressions.md). Emoji Plus is prohibited and cannot be enabled by a scheme patch.

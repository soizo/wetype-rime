# 可选的 Rime 原生扩展

这些文件不会由安装或同步命令自动启用。先完成正常安装，再按需复制到项目 `rime/`，运行 `python3 -m wetype sync-config`，最后在鼠须管菜单中重新部署。

## 自定义词汇

将 `phrases/wetype.custom.yaml` 放到 `rime/wetype.custom.yaml`，将 `phrases/wetype_phrases.txt` 放到 `rime/wetype_phrases.txt`。编辑短语表，字段用 Tab 分隔。输入 `myphrase` 时会优先出现“我的自定义短语”，其他输入继续走 WeType 引擎。此处是额外的文字短语表，不影响原生 bin 词库和学习数据。

也可以使用 `*.dict.yaml` 和 `table_translator` 添加编译式文字词典；词典文件放在 `rime/` 后会随同步命令复制。

## WeType 原生 Emoji 推荐

先安装包含原生 Emoji 支持的新接口版本，再将 `native-emoji/wetype.custom.yaml` 合并到项目的 `rime/wetype.custom.yaml`，同步并重新部署。它增加一个 Emoji 开关，默认开启。启用后如输入 `xiaolian`、`haha`，Emoji 会作为 WeType 原生候选参与排序和选词反馈；使用独立小鹤方案时改用其双拼编码，例如 `xnlm`。开关变化时会清空正在输入的组合并重建独立会话。无需 OpenCC Emoji 文件；不启用云端表情或微信专用的内部表情编码。

`WeType小鹤大陆` 已内置这项配置，不需要重复添加开关或补丁。

## 指定输入强制替换

将 `force/wetype.custom.yaml` 放到 `rime/wetype.custom.yaml`，将 `force/wetype_force.lua` 放到 `rime/lua/wetype_force.lua`。编辑 Lua 文件中的输入映射。完整输入 `myname` 时，候选列表只显示“自定义姓名”，按空格确认后上屏；其他输入的候选原样通过。不自动模拟按键或直接提交文字。

## Rime 模糊音

安装支持模糊音桥接的新接口后，将 `fuzzy/wetype.custom.yaml` 合并到 `rime/wetype.custom.yaml`，同步并重新部署。独立小鹤方案用 `wetype_flypy.custom.yaml`。共享规则位于项目 `rime/wetype_fuzzy.yaml`，默认 `in / ing` 双向模糊，可以直接修改或追加 Rime `derive` 规则；双向模糊需要两条规则。派生使用 librime 自己的拼写运算，WeType 引擎模糊音保持关闭。

全拼输入 `qin`，小鹤输入 `qb`，均可补充 `qing` 的“请”等候选。原输入前三项保留，补充候选带“模糊”注释。WeType小鹤大陆已内置开关，不要再重复添加。当前支持全拼、小鹤、完整音节和最多 32 字符；其他布局、大小写混合及长输入保留原生路径。详细边界见 [用户配置说明](../../rime/README.md)。

## 繁简转换

将 `traditional/wetype.custom.yaml` 放到 `rime/wetype.custom.yaml`。重新部署后，方案选项菜单会出现“简体 / 繁體”，默认简体，打开繁体时使用 OpenCC 的 `s2t.json` 转换候选输出。需要鼠须管提供对应 OpenCC 配置和数据。过滤器转换的是候选文字，原生引擎的学习反馈仍引用原候选。

The example excludes `user_table` through Rime's standard `excluded_types` setting, preserving custom phrases verbatim for both native WeType and official Rime phrase candidates.

## 同时启用或用于其他方案

各功能目录中的 `wetype.custom.yaml` 是独立示例，不要依次覆盖同一个文件。要组合使用，将所需条目合并进同一个 `patch:` 映射，避免重复键；多个开关应合并为一次列表追加。强制替换过滤器放在繁简转换之前，使固定输出也能按需转为繁体。

示例默认作用于统一方案。要作用于独立方案，将补丁文件改名为对应的 `<schema_id>.custom.yaml`，例如 `wetype_quanpin.custom.yaml`；短语表和 Lua 文件可以共用。

参考 Rime 官方的 [配置语法](https://github.com/rime/home/wiki/Configuration) 和 [定制指南](https://github.com/rime/home/wiki/CustomizationGuide)。

## Optional native dictionaries

Merge `dictionaries/wetype.custom.yaml` into the target scheme's normal patch to
enable `wetype.dict.yaml`. Its `import_tables` list selects independent ordinary
Rime dictionary files, including the empty `custom.dict.yaml` starter.
Set `translator/dictionary: null` and deploy to disable imports. This extension is
disabled by default; [the dictionary guide](../../docs/dictionaries.md) explains
entry formats, native weights and retained learning.

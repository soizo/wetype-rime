# WeTypeRime 用户配置

本目录是方案配置的源文件，直接修改即可。安装器逐文件复制，不再根据 Python 模板生成方案。

- `wetype.schema.yaml`：统一方案，包含输入模式菜单。
- `wetype_*.schema.yaml`：11 个独立输入模式，各自可调整。
- `wetype_flypy_gov.schema.yaml`：个人定制的 `WeType小鹤大陆`，继承小鹤基础方案，单独启用 WeType 原生 Emoji 推荐和大陆字形简繁转换。需要用户 Rime 中已有 `s2t.json`、`govrime_t2gov_keep_simp.json` 及其 OpenCC 字典；这些外部资源不会打包进项目。左 Shift 切换简繁，右 Shift 标点与原“小鹤双拼大陆”一致。
- `*.custom.yaml`：你按 Rime 原生格式添加的补丁。
- `*.dict.yaml`、`*.txt`：你自行添加的 Rime 文字词典、短语表。
- `lua/`：接口加载器，以及你按需添加的 Lua 扩展。

The schemes use the native engine and dictionaries, inherit Rime’s default key bindings and recogniser patterns, and retain uppercase input for the native engine. Custom phrases use the ordinary `custom_phrase` section and `user_dict` text table; candidates and learning remain native unless an official `table_translator@custom_phrase` is explicitly configured. See [user-word configuration](../docs/user-words.md). Personal changes belong in `<schema_id>.custom.yaml`, followed by Squirrel deployment. Binary resources remain in the independent runtime.

## 修改与应用

可直接调整各方案的 `schema/name`、`menu/page_size`、`key_binder`、`punctuator`、`ascii_composer`；也可以用对应的 `<schema_id>.custom.yaml` 加补丁。例如独立小鹤方案使用 `wetype_flypy.custom.yaml`。

```sh
python3 -m wetype sync-config
```

然后在鼠须管菜单中重新部署。首次使用需先运行 `install-rime` 完成接口安装。更换 Rime 用户目录时给同步命令加 `--rime-dir /path/to/Rime`。

大陆定制方案属于可选方案，不自动加入基础 12 方案的菜单。要使用，在 Rime 的 `default.custom.yaml` 中 `schema_list` 下增加 `- schema: wetype_flypy_gov` 后重新部署。它复用 WeType 小鹤输入和已有学习数据，没有挂载雾凇词库、英文词库或依赖雾凇内部结构的纠错/辅码 Lua。

Both mainland OpenCC stages use `excluded_types: [user_table]`. Custom phrases keep their literal text through script and glyph conversion, including when phrase priority is disabled. Other engine candidates continue to follow the script switch.

同步范围是本目录顶层的 YAML、TXT 和 `lua/` 下直接存放的 Lua 文件；不会自动同步 `examples/`。项目内同名文件是配置来源，目标中有差异的旧文件会备份到项目 `.local/backups/config-*`。只在目标目录存在的个人文件会保留。不要编辑目标 `build/` 里的部署产物。

`wetype.runtime.yaml` 只在安装目标生成，保存引擎启动器、接口模块和日志的路径。各方案通过 `wetype.runtime:/wetype` 引用这些路径，本目录不能放同名文件。只改 YAML 不需要重新提取引擎或编译接口。

## 按需扩展

示例在 [`examples/rime-extensions/`](../examples/rime-extensions/README.md)，包含自定义短语、精确输入替换和繁简转换。它们是独立的启用示例，默认不安装、不启用。可自由增加 Rime 原生 translator、filter 和 recognizer；不要移除原生接口所需的 `wetype_speller`、`wetype_translator` 和 Lua 加载器。

`native-emoji` 示例通过 `wetype/emoji_option: emoji` 绑定 Rime 的 `emoji` 开关。候选由 WeType 的本地二进制词库直接生成，不使用 `emoji.json` 映射；关闭开关后会重建会话并清空正在输入的组合，防止旧候选被选中。基础方案默认不绑定此开关；大陆定制方案默认开启。此能力需要更新后的接口与 worker，首次升级需运行 `install-rime`，后续开关和方案配置修改才只需 `sync-config`。

## Rime 模糊音

`wetype_fuzzy.yaml` 集中保存模糊音规则、通用拼音音节名和小鹤键位运算。默认规则只有 `in / ing` 双向模糊；n/l、平翘舌等规则留有注释，按需开启。这里的音节表不包含字词，不替换 WeType 的 bin 词库。

规则使用 Rime 原生 `speller/algebra` 语法，例如：

```yaml
algebra:
  - derive/in$/ing/
  - derive/ing$/in/
```

WeType小鹤大陆默认开启，方案菜单可切换“精准音 / 模糊音”。全拼或独立小鹤方案可使用 `examples/rime-extensions/fuzzy/` 中的补丁。基础 12 方案默认不启用；其他双拼布局和五笔暂不生成模糊候选。

适配器使用 librime 的拼写运算派生拼音，再由 WeType 提供候选，不启用 WeType 的模糊音字段。小鹤先做拼音模糊派生，再映射到双拼编码，所以输入 `qb`（qin）也可出现 `qing` 的“请”等候选，注释为“模糊”。原输入前三个候选保留，模糊候选随后插入并去重；选词反馈仍送回对应的原生引擎会话。

当前只对完整音节组成、32 字符以内的小写输入补充候选，每条路径最多两个模糊音节、最多三个扩展拼写。模糊分支只提供覆盖整个输入的候选；原路径仍支持部分选词。含大写的输入、无法被音节表完整识别的输入、长输入及不支持的布局继续使用原生路径。切换开关会清空正在输入的组合。首次升级需安装新的接口，之后修改规则只需同步配置并重新部署。

## Optional dictionaries

Ordinary `*.dict.yaml` vocabulary and `import_tables` are supported through
`translator/dictionary`, which is null by default. The installed
`wetype.dict.yaml` and `custom.dict.yaml` are empty starter files. Merge
`translator/dictionary: wetype` into the target scheme's `.custom.yaml`, add
full-pinyin entries, then deploy. Set the dictionary back to `null` to disable it.
WeType handles matching, initial weight ranking and native learning. Existing
personal `*.dict.yaml` files are never overwritten by configuration sync.
See [optional dictionaries](../docs/dictionaries.md) for domain tables, weights,
learning retention and limits.

## Bundled native cell dictionaries

The native binary packs are selected separately from Rime `*.dict.yaml` files.
All base schemes set `wetype/cell_dicts: []`, so they remain disabled until a
scheme's `.custom.yaml` selects the desired identifiers. The current engine
supplies `cell_dict.sc`, `cell_dict.ssrc` and `cell_dict.hf`. Merge the
[native cell example](../examples/rime-extensions/native-cell-dictionaries/wetype.custom.yaml)
into the relevant patch and deploy; replace the list with `[]` to disable them.
The native engine loads and ranks these resources locally. See the
[cell dictionary guide](../docs/native-cell-dictionaries.md) for details.

# WeTypeRime 用户配置

本目录是方案配置的源文件，直接修改即可。安装器逐文件复制，不再根据 Python 模板生成方案。

- `wetype.schema.yaml`：统一方案，包含输入模式菜单。
- `wetype_*.schema.yaml`：11 个独立输入模式，各自可调整。
- `wetype_flypy_gov.schema.yaml`：个人定制的 `WeType小鹤大陆`，继承小鹤基础方案，单独启用 WeType 原生 Emoji 推荐和大陆字形简繁转换。需要用户 Rime 中已有 `s2t.json`、`govrime_t2gov_keep_simp.json` 及其 OpenCC 字典；这些外部资源不会打包进项目。左 Shift 切换简繁，右 Shift 标点与原“小鹤双拼大陆”一致。
- `*.custom.yaml`：你按 Rime 原生格式添加的补丁。
- `*.dict.yaml`、`*.txt`：你自行添加的 Rime 文字词典、短语表。
- `lua/`：接口加载器，以及你按需添加的 Lua 扩展。

默认方案只提供原生引擎候选、基本按键和标点，不附带扩展词汇、强制替换、繁简转换过滤器。bin 词库保存在独立引擎目录，用户词汇作为 Rime 侧候选加入，不会改写 bin 文件。

## 修改与应用

可直接调整各方案的 `schema/name`、`menu/page_size`、`key_binder`、`punctuator`、`ascii_composer`；也可以用对应的 `<schema_id>.custom.yaml` 加补丁。例如独立小鹤方案使用 `wetype_flypy.custom.yaml`。

```sh
python3 -m wetype sync-config
```

然后在鼠须管菜单中重新部署。首次使用需先运行 `install-rime` 完成接口安装。更换 Rime 用户目录时给同步命令加 `--rime-dir /path/to/Rime`。

大陆定制方案属于可选方案，不自动加入基础 12 方案的菜单。要使用，在 Rime 的 `default.custom.yaml` 中 `schema_list` 下增加 `- schema: wetype_flypy_gov` 后重新部署。它复用 WeType 小鹤输入和已有学习数据，没有挂载雾凇词库、英文词库或依赖雾凇内部结构的纠错/辅码 Lua。

同步范围是本目录顶层的 YAML、TXT 和 `lua/` 下直接存放的 Lua 文件；不会自动同步 `examples/`。项目内同名文件是配置来源，目标中有差异的旧文件会备份到项目 `.local/backups/config-*`。只在目标目录存在的个人文件会保留。不要编辑目标 `build/` 里的部署产物。

`wetype.runtime.yaml` 只在安装目标生成，保存引擎启动器、接口模块和日志的路径。各方案通过 `wetype.runtime:/wetype` 引用这些路径，本目录不能放同名文件。只改 YAML 不需要重新提取引擎或编译接口。

## 按需扩展

示例在 [`examples/rime-extensions/`](../examples/rime-extensions/README.md)，包含自定义短语、精确输入替换和繁简转换。它们是独立的启用示例，默认不安装、不启用。可自由增加 Rime 原生 translator、filter 和 recognizer；不要移除原生接口所需的 `wetype_speller`、`wetype_translator` 和 Lua 加载器。

`native-emoji` 示例通过 `wetype/emoji_option: emoji` 绑定 Rime 的 `emoji` 开关。候选由 WeType 的本地二进制词库直接生成，不使用 `emoji.json` 映射；关闭开关后会重建会话并清空正在输入的组合，防止旧候选被选中。基础方案默认不绑定此开关；大陆定制方案默认开启。此能力需要更新后的接口与 worker，首次升级需运行 `install-rime`，后续开关和方案配置修改才只需 `sync-config`。

# wetype-rime

Offline WeType engine integration for Rime on Apple Silicon macOS.

Use WeType's local engine, dictionaries and learning through Squirrel. The Rime scheme is displayed as **WeTypeRime**; its internal schema and Python module remain `wetype` for compatibility. Version 0.1.0 is an experimental, macOS-only source release. Engine extraction is not locked to a single WeType version: you can inspect and prepare local releases, then test their compatibility before installation.

## Table of Contents

- [Install](#install)
- [Usage](#usage)
  - [Extracting Other Versions](#extracting-other-versions)
- [Configuration](#configuration)
- [Input Modes](#input-modes)
- [Privacy and Local Data](#privacy-and-local-data)
- [Compatibility](#compatibility)
- [Development](#development)
- [Acknowledgements](#acknowledgements)
- [Contributing](#contributing)
- [License](#license)

## Install

### Dependencies

- An Apple Silicon Mac.
- [Squirrel](https://github.com/rime/squirrel) with librime-lua available.
- A local macOS WeType application, from which to prepare the engine. Version **3.0.1, build 680** is validated; other versions can be extracted and tested.
- Python 3.11 or newer and Xcode Command Line Tools.
- Boost headers and a local checkout of the official [librime source](https://github.com/rime/librime). Use headers compatible with your installed Squirrel/librime build.

Preparation discovers the executable from Info.plist, finds the dictionary index, copies all bundled frameworks and records actual versions and SHA-256 hashes. It does not require a release-specific hash. This repository does not contain or download WeType's proprietary engine or dictionaries.

```sh
git clone https://github.com/soizo/wetype-rime.git
cd wetype-rime

python3 -m wetype inspect-macos '/Library/Input Methods/WeType.app'
python3 -m wetype prepare-macos '/Library/Input Methods/WeType.app'
python3 -m wetype verify
python3 -m wetype probe nihao --state install-check
python3 -m wetype install-rime --rime-source '/path/to/librime'
```

Replace `/path/to/librime` with your local source directory. Boost headers default to `/opt/homebrew/include`; set `WETYPE_BOOST_INCLUDE` if yours are elsewhere. `WETYPE_RIME_SOURCE` can also supply the librime source directory.

After installation, use Squirrel's **Deploy** menu item, then select **WeTypeRime** in the scheme menu. The installer backs up the scheme list and adds its entries alongside your existing schemes.

Daily operation uses an independent copy of the engine and resources. After a successful installation, uninstalling the official WeType application does not remove this copy or its learning data. The project directory, Python and build tools are not needed for daily typing.

## Usage

```text
1. Switch to Squirrel.
2. Open the scheme menu with Control + ` or F4.
3. Select wetype and use Chinese mode.
4. Type nihao, then press Space to commit the selected candidate.
```

Use number keys to select candidates, `-` / `=` or Page Up / Page Down to change pages, Esc to cancel, and Return to commit the raw input. Partial selections and selection feedback are passed back to the engine.

The engine's syllable boundaries appear in the preedit, for example `ni'hao'zhong'guo`. Uppercase and mixed-case input such as `ABC`, `OpenAI` and `iPhone` is forwarded unchanged.

The scheme options include **前文联想开 / 前文联想关** to enable or disable preceding-text context. When enabled, a bounded prefix from the current editor is supplied locally to the engine once per composition. Editors that cannot provide context continue with ordinary input.

### Extracting Other Versions

Inspect a local app without loading its code, then prepare each version in its own directory:

```sh
python3 -m wetype inspect-macos '/path/to/WeType.app'
python3 -m wetype prepare-macos '/path/to/WeType.app' --output .local/engines/my-version
python3 -m wetype verify .local/engines/my-version
python3 -m wetype probe nihao --engine .local/engines/my-version --state compatibility
```

Preparation discovers the executable named in `Info.plist` and searches for a usable dictionary index. It copies the full dictionary directory, including unindexed model files, and the application's complete `Frameworks` directory. Each copy records the actual app version, build, source hash and file hashes.

If discovery finds multiple indexes, specify the directory containing the intended `index.json`:

```sh
python3 -m wetype inspect-macos '/path/to/WeType.app' --resources '/path/to/dictionary-directory'
python3 -m wetype prepare-macos '/path/to/WeType.app' --resources '/path/to/dictionary-directory' --output .local/engines/my-version
```

Nested dictionary paths are supported. Preparation checks indexed files, dictionary loader fields and any MD5/SHA-256 checksums supplied by the index. A missing, ambiguous or incompatible index produces an error rather than selecting a dictionary set silently.

After a successful probe and the [native checks](docs/development.md#verification), install the chosen copy explicitly:

```sh
python3 -m wetype install-rime --engine .local/engines/my-version --runtime "$HOME/Library/Application Support/WetypeRime-my-version" --rime-source '/path/to/librime'
```

Use a different `--output` for each prepared copy and a different `--state` when testing releases whose learning formats may differ. Preparation refuses to overwrite an existing output directory. Use a separate `--runtime` when installing a different engine or resource set; installation rejects a conflicting copy already present there. Installation updates the wetype schemes to use the selected runtime.

The default prepared-engine directory remains `.local/engines/macos-3.0.1-680` for existing workflows. Its name is a legacy default, not a version restriction. When using another directory, pass its path directly to `verify` and use `--engine` with `probe` and `install-rime`, as shown above.

### CLI

```sh
python3 -m wetype --version
python3 -m wetype verify
python3 -m wetype probe nihao --state demo
python3 -m wetype probe nihao --state demo --select 拟好
python3 -m wetype probe xiaolian --state emoji-demo --emoji
```

The probe uses a separate named test state, not your daily learning data. To disable the installed schemes while preserving engine resources and learning:

```sh
python3 -m wetype disable-rime
```

Deploy Squirrel again after disabling.

## Configuration

Editable scheme YAML lives in [`rime/`](rime/README.md): the unified `wetype.schema.yaml` and all 11 standalone `wetype_*.schema.yaml` files. Installation copies these files verbatim. Change names, page size, key bindings and punctuation directly, or add native Rime `*.custom.yaml` patches.

```sh
python3 -m wetype sync-config
```

Then use Squirrel's **Deploy** menu item. Configuration-only changes do not rebuild the adapter or recopy the engine. Changed destination files are backed up in `.local/backups/config-*`; files present only in the destination are preserved.

The engine, binary dictionaries and adapter live in the independent runtime. Machine-specific module and launcher paths are generated separately in the installed `wetype.runtime.yaml`, which the schemes include. The default schemes have no extra phrase table, forced replacements or script conversion. [Optional examples](examples/rime-extensions/README.md) show how to add custom vocabulary, exact-input replacements and a simplified/traditional toggle using Rime's own translators and filters.

The optional [`WeType小鹤大陆` variant](rime/wetype_flypy_gov.schema.yaml) inherits the basic Xiaohe scheme and adds mainland traditional glyph conversion, native WeType Emoji recommendations and personal Shift-key punctuation behavior. Its script conversion uses existing OpenCC resources from the user's Rime installation; Emoji candidates come directly from WeType's local engine and binary dictionaries, with native ranking and selection feedback. See [configuration notes](rime/README.md) for dependencies, menu registration and the opt-in Emoji patch for other schemes. The original 12 basic schemes keep their defaults.

Optional fuzzy spelling uses Rime's own `speller/algebra` through the adapter, with editable rules in [`rime/wetype_fuzzy.yaml`](rime/wetype_fuzzy.yaml); WeType's fuzzy options stay disabled. Full pinyin and Xiaohe are supported, with `in / ing` as the initial rules. The mainland Xiaohe variant enables this by default and exposes an on/off switch. Other base schemes can opt in using the [fuzzy example](examples/rime-extensions/fuzzy/wetype.custom.yaml). See the [configuration guide](rime/README.md#rime-模糊音) for input and candidate limits.

[User-word management](docs/user-words.md) uses Rime's standard text-table format and WeType's native user-word store. Edit `wetype_user_words.txt` in the Rime user directory and deploy to add, update or remove configured phrases. Rime's Control + Delete candidate action also reaches the native engine. Candidate ranking and selection learning remain with WeType.

## Input Modes

The unified **WeTypeRime** scheme offers all 11 input modes through Rime's option menu. Each mode also has its own standalone scheme, displayed with a `WeTypeRime 拼音`-style name. The internal schema IDs remain `wetype` and `wetype_*` so existing configurations keep working.

| Type | Available modes |
| --- | --- |
| Pinyin | Full pinyin |
| Double pinyin | Xiaohe, Ziranma, Microsoft, Sogou, Ziguang, Intelligent ABC, Pinyin Jiajia |
| Wubi | 86, 98, New Century |

Microsoft, Sogou and Ziguang double pinyin accept semicolon as part of the code. Other modes keep its punctuation behavior. In Wubi, entering another letter after a complete four-letter code confirms the preceding candidate.

## Privacy and Local Data

Engine workers run under a macOS policy that denies network access to the entire worker process and its children. Loading is refused without the required restriction. Communication with Rime uses local anonymous pipes, with no network listener, account login, cloud dictionary or synchronization service.

The optional preceding-text feature does not read the clipboard. The adapter's diagnostic logs record context availability and byte counts, not the preceding text itself.

Installed files live in `~/Library/Application Support/WetypeRime/`:

| Path | Contents |
| --- | --- |
| `bin/` | Integration binaries and launchers |
| `engine/` | Independent engine, dependencies and dictionary resources |
| `state/learning/` | Shared learning for full and double pinyin |
| `state/wubi*/learning/` | Separate Wubi learning data |
| `state/logs/` | Local `.log` diagnostics and crash reports |

Configuration backups are kept in the checkout's `.local/backups/` directory. Proprietary resources, learning data and logs are excluded from Git.

## Compatibility

The validated configuration is Apple Silicon macOS, WeType 3.0.1 (680), Squirrel 1.1.2 and its bundled librime 1.16.0. Other engine or frontend versions need separate validation.

| Check | What it establishes |
| --- | --- |
| `inspect-macos` | Local app metadata and a discoverable dictionary index, without loading engine code |
| `prepare-macos` | The binary can be converted and its resources copied and checked |
| `verify` | Copied files and links still match the local manifest |
| `probe` | The engine can produce candidates in an isolated process, with a 30-second execution timeout |

Unknown source binaries are recorded as `unvalidated`. Extraction and file verification do not establish runtime compatibility. Existing format-1 manifests remain supported.

Future releases can be attempted without adding a version or hash to an allowlist. They must still use a supported arm64 Mach-O layout, dictionary index schema and private engine interface. Changes to these formats or interfaces require adapter updates; a successful probe alone does not validate every input mode or long-term stability. The current commands operate on local macOS applications and do not download versions or extract Windows/Android packages.

First activation includes engine and model loading. Latency can vary with model state and hardware. If the engine fails, the adapter preserves raw input and reports the error through candidate comments and local logs. Long-term stability remains an area for further work.

This release contains the macOS implementation only.

## Development

See [development notes](docs/development.md) for the source layout and opt-in engine tests, and [the changelog](CHANGELOG.md) for release history.

Tests that do not require proprietary resources:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 tests/smoke_client.py
```

## Acknowledgements

Built on [Rime](https://github.com/rime/librime), [Squirrel](https://github.com/rime/squirrel) and [librime-lua](https://github.com/hchunhui/librime-lua). WeType and its engine resources belong to their respective owners. This is an independent project.

## Contributing

Use [GitHub Issues](https://github.com/soizo/wetype-rime/issues) for bugs and questions. Pull requests are welcome; please discuss substantial changes in an issue first. Include the relevant versions and reproducible steps, and keep engine binaries, personal text, dictionaries and learning data out of submissions.

## License

Copyright © 2026 soizo. The project source is licensed under **GPL-3.0-only**; see [LICENSE](LICENSE).

WeType binaries, models and dictionary resources are not included and are not licensed under this project's GPL license.

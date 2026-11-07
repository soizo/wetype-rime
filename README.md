# wetype-rime

Offline WeType engine integration for Rime on Apple Silicon macOS.

Use WeType's local engine, dictionaries and learning through Squirrel. The Rime scheme and Python module are named `wetype`. Version 0.1.0 is an experimental, macOS-only source release.

## Table of Contents

- [Install](#install)
- [Usage](#usage)
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
- A local copy of WeType **3.0.1, build 680**, from which to prepare the engine.
- Python 3.11 or newer and Xcode Command Line Tools.
- Boost headers and a local checkout of the official [librime source](https://github.com/rime/librime). Use headers compatible with your installed Squirrel/librime build.

The installer checks the WeType executable's SHA-256 and rejects unsupported binaries. This repository does not contain or download WeType's proprietary engine or dictionaries.

```sh
git clone https://github.com/soizo/wetype-rime.git
cd wetype-rime

python3 -m wetype prepare-macos '/Library/Input Methods/WeType.app'
python3 -m wetype install-rime --rime-source '/path/to/librime'
```

Replace `/path/to/librime` with your local source directory. Boost headers default to `/opt/homebrew/include`; set `WETYPE_BOOST_INCLUDE` if yours are elsewhere. `WETYPE_RIME_SOURCE` can also supply the librime source directory.

After installation, use Squirrel's **Deploy** menu item, then select **wetype** in the scheme menu. The installer backs up the scheme list and adds its entries alongside your existing schemes.

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

### CLI

```sh
python3 -m wetype --version
python3 -m wetype verify
python3 -m wetype probe nihao --state demo
python3 -m wetype probe nihao --state demo --select 拟好
```

The probe uses a separate named test state, not your daily learning data. To disable the installed schemes while preserving engine resources and learning:

```sh
python3 -m wetype disable-rime
```

Deploy Squirrel again after disabling.

## Input Modes

The unified **wetype** scheme offers all 11 input modes through Rime's option menu. Each mode also has its own standalone scheme.

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

First activation includes engine and model loading. Latency can vary with model state and hardware. If the engine fails, the adapter preserves raw input and reports the error through candidate comments and local logs. Long-term stability and correction of accidental learning remain areas for further work.

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

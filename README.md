# WetypeRime _(wetype-rime)_

Offline WeType engine integration for Rime on Apple Silicon macOS.

WetypeRime runs WeType's local engine, dictionaries and learning through [Squirrel](https://github.com/rime/squirrel). The displayed schemes use names such as **微信输入** and **微信拼音**; the repository name and Python module remain `wetype-rime` and `wetype` for compatibility.

This is an experimental, source-only macOS release. It does not include or download WeType's proprietary engine or dictionaries.

## Install

### Requirements

- Apple Silicon Mac with [Squirrel](https://github.com/rime/squirrel) and librime-lua.
- A local WeType installation. Version 3.0.1 (build 680) is validated.
- Python 3.11 or later and Xcode Command Line Tools.
- Boost headers and a local checkout of [librime](https://github.com/rime/librime).

```sh
git clone https://github.com/soizo/wetype-rime.git
cd wetype-rime

python3 -m wetype inspect-macos '/Library/Input Methods/WeType.app'
python3 -m wetype prepare-macos '/Library/Input Methods/WeType.app'
python3 -m wetype verify
python3 -m wetype probe nihao --state install-check
python3 -m wetype install-rime --rime-source '/path/to/librime'
```

Boost defaults to `/opt/homebrew/include`. Set `WETYPE_BOOST_INCLUDE` if necessary, or use `WETYPE_RIME_SOURCE` instead of `--rime-source`.

Deploy Squirrel, then select **微信输入**. Installation preserves existing schemes and copies the engine and resources into the Rime profile, so the checkout and build tools are not required for daily use.

To prepare another local WeType version, use a separate output and state directory:

```sh
python3 -m wetype prepare-macos '/path/to/WeType.app' --output .local/engines/my-version
python3 -m wetype probe nihao --engine .local/engines/my-version --state compatibility
python3 -m wetype install-rime --engine .local/engines/my-version \
  --runtime "$HOME/Library/Rime/wetype-my-version" --rime-source '/path/to/librime'
```

## Usage

```text
1. Switch to Squirrel.
2. Open the scheme menu with Control + ` or F4.
3. Select 微信输入.
4. Type nihao and press Space to commit the selected candidate.
```

The unified scheme provides full pinyin, seven double-pinyin layouts and three Wubi layouts. Standalone schemes are installed as well.

Edit `wetype-candidate-rules.custom.yaml` in your Rime user directory to pin or block WeType candidates. Candidate-management shortcuts are disabled by default. See [candidate rules](docs/user-words.md#candidate-pinning-and-blocking).

To disable WetypeRime while keeping its engine resources and learning data:

```sh
python3 -m wetype disable-rime
```

Deploy Squirrel again after disabling it.

## Configuration

Edit the installed `<schema_id>.custom.yaml`, then deploy Squirrel. Shared defaults can be patched in `wetype_common.custom.yaml`; per-scheme settings take precedence.

Detailed guides cover [scheme options](data/README.md), [user words and candidate preferences](docs/user-words.md), [optional dictionaries](docs/dictionaries.md), [local expressions](docs/expressions.md), [native cell dictionaries](docs/native-cell-dictionaries.md) and [Rime extensions](examples/rime-extensions/README.md).

Run `python3 -m wetype sync-config` to update supplied presets without rebuilding the adapter or recopying the engine. Personal configuration and dictionary files are preserved.

## Privacy and Compatibility

Engine workers and their children run under a macOS policy that denies network access. The integration uses local pipes and has no login, cloud dictionary or synchronisation service. Optional editor context is processed locally; diagnostics record sizes and availability, not text.

The validated combination is Apple Silicon macOS, WeType 3.0.1 (680), Squirrel 1.1.2 and librime 1.16.0. Other versions require their own probe and native checks. The current release supports macOS only.

## Development

See the [development guide](docs/development.md) and [changelog](CHANGELOG.md).

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 tests/smoke_client.py
```

## Acknowledgements

Built on [Rime](https://github.com/rime/librime), [Squirrel](https://github.com/rime/squirrel) and [librime-lua](https://github.com/hchunhui/librime-lua). WeType and its engine resources belong to their respective owners. This is an independent project.

## Contributing

Use [GitHub Issues](https://github.com/soizo/wetype-rime/issues) for questions and bug reports. Pull requests are welcome; discuss substantial changes in an issue first and keep engine binaries, personal text, dictionaries and learning data out of submissions.

## License

Copyright © 2026 soizo. Project source is licensed under [GPL-3.0-only](LICENSE). WeType binaries, models and dictionary resources are not included or covered by this licence.

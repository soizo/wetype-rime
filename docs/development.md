# Development

This release targets Apple Silicon macOS and the engine/frontend versions listed in the README. It contains the integration source and tests, with no proprietary engine files or personal input data.

## Source layout

- `wetype/macos.py`: discover and prepare a version-independent local engine copy, verify resources and run isolated probes.
- `wetype/integration.py`: build the Rime component, stage independent runtime resources and deploy schemes.
- `wetype/modes.json`: input-mode catalog used to generate schemas and native settings.
- `native/worker.cpp`: persistent engine host and local pipe protocol.
- `native/rime_wetype.cpp`: official Rime Translator, Candidate, notifications and module entry point.
- `native/surrounding_text.mm`: bounded IMK preceding-text access during input events.
- `native/offline.sb`: network restriction and source-application isolation policy.
- `rime/`: all 12 editable scheme YAML files and the small librime-lua module loader. Schemes are copied verbatim, not generated from modes.json.
- `examples/rime-extensions/`: opt-in native Rime phrase, exact-input replacement and script-conversion examples; never installed automatically.
- `tests/`: isolated unit, transport, native-engine and real-librime checks.

## Build requirements

Use Python 3.11+, local Xcode Command Line Tools, Boost headers, compatible official librime source headers and a supported Squirrel installation. No build dependency is downloaded automatically.

```sh
export WETYPE_RIME_SOURCE='/path/to/librime'
export WETYPE_BOOST_INCLUDE='/opt/homebrew/include'
```

Build outputs are written to `build/`. Engine copies, test state and backups remain under `.local/`; both directories are ignored by Git. Do not use a live input-method profile for experimental engine state.

## Verification

Run lightweight tests without WeType resources:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
python3 tests/smoke_client.py
```

After preparing the supported local engine, opt into native-engine checks:

```sh
python3 tests/smoke_native.py
python3 tests/smoke_worker.py
python3 tests/smoke_context.py
python3 tests/smoke_rime.py --reloads 1
python3 tests/smoke_config.py
```

The checks cover real candidates, selection feedback, partial selection, persistent learning, uppercase input, syllable boundaries, deletion and context isolation. The real-librime test also exercises the unified options, standalone modes, preceding-text behavior and module reinitialization. Native snapshot checks use a test-only command that is not compiled into the production worker.

`smoke_config.py` uses already-built binaries and an isolated profile to deploy the optional phrase, exact-input replacement and simplified/traditional examples together. Use `--feature phrases`, `--feature force` or `--feature traditional` to test each independently. It requires the validated macOS Squirrel build and never changes a live profile.

`python3 tests/smoke_config.py --feature mainland` tests the optional `wetype_flypy_gov` scheme, including Xiaohe input, left-Shift script switching, right-Shift punctuation and full-width space. It copies the existing user's OpenCC resources into its isolated fixture; those resources are not distributed by this project.

`python3 tests/smoke_config.py --feature native-emoji` tests native engine Emoji ranking, numeric selection, feedback, composition invalidation and on/off/on switching. Mainland checks exercise the same behavior in Xiaohe mode. `native/session_options.h` records the validated Mac 3.0.1 session metadata: local normal-Emoji recommendation at byte 45 and local normal-Emoji input at byte 52. The worker pipe accepts `OPEN <mode> [0|1]`; omitted means disabled. Rime reads the option named by `wetype/emoji_option`, closes the old session and opens a new one when it changes. Raw candidates retain native selection IDs and coverage. New ABI layouts require separate verification; this is Unicode Emoji support, not cloud sticker retrieval or WeChat-internal emoticon rendering.

For paced input and held-delete measurements, add `--timing` to `smoke_rime.py`. Background model loading can change latency, so report the test duration and model state when comparing results.

The real-librime test creates an isolated profile. `--user-profile /path/to/Rime --baseline-schema schema_id` optionally copies static configuration for coexistence checks; it does not copy personal user databases. Successful fixtures are compacted to local `.log` files, while failures remain available for diagnosis. No test restarts the user's Squirrel process.

## Integration boundaries

`sync_configs` copies project YAML/TXT and Lua files independently of engine staging. Conflicting destination files are backed up; destination-only personal files are left alone. `stage` generates only `wetype.runtime.yaml` for machine-specific interface paths. Every scheme includes `wetype.runtime:/wetype` and declares its own mode. `modes.json` remains the engine ABI catalog; adding a new input mode requires updating both that catalog and its scheme, while ordinary configuration edits need only synchronization and Rime deployment.

The adapter uses Rime's official C++ component and module interfaces. Lua only loads the native module. Candidates retain their session, revision ticket and engine selection identifier so selection feedback cannot be sent to an unrelated query.

The engine and dependent frameworks are copied from a local WeType application. The executable copy is converted into a loadable bundle; algorithm code and model data are not patched. The original application's directory is denied during worker execution.

Preceding text is sampled once per composition through the current IMK client, excluding marked text, with a maximum request of 256 UTF-16 units. Unsupported clients and secure input fall back to ordinary input. This bridge is specific to the validated Squirrel implementation and needs review when that frontend changes.

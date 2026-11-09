# Development

This release targets Apple Silicon macOS and the engine/frontend versions listed in the README. It contains the integration source and tests, with no proprietary engine files or personal input data.

## Source layout

- `wetype/macos.py`: discover and prepare a version-independent local engine copy, verify resources and run isolated probes.
- `wetype/integration.py`: build the Rime component, stage independent runtime resources and deploy schemes.
- `wetype/modes.json`: input-mode catalogue used to generate native settings; schema IDs are kept in sync explicitly.
- `src/worker.cpp`: persistent engine host and local pipe protocol.
- `src/rime_wetype.cpp`: official Rime Translator, Candidate, notifications and module entry point.
- `src/surrounding_text.mm`: bounded IMK editor-context access during input events.
- `src/offline.sb`: network restriction and source-application isolation policy.
- `data/`: the shared Rime defaults, 12 base schemes and small librime-lua module loader. Presets are copied verbatim and use native Rime inheritance, not Python-generated schemas.
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
python3 tests/smoke_layout.py
python3 tests/smoke_config.py
python3 tests/smoke_user_words.py
```

The checks cover real candidates, selection feedback, partial selection, persistent learning, uppercase input, syllable boundaries, deletion and context isolation. The real-librime test also exercises the unified options, standalone modes, editor-context behaviour and module reinitialisation. Native snapshot checks use a test-only command that is not compiled into the production worker.

`smoke_layout.py` checks default in-profile installation through a symlink with spaces, relative component bindings, shared and per-scheme patch precedence, legacy candidate-pin import and native commit. Unit checks cover profile-specific defaults, migration without learning-data copies, idempotence and preservation of personal files. New profiles register only the unified scheme; explicitly requested and existing standalone menu entries are retained.

`smoke_config.py` uses already-built binaries and an isolated profile to deploy the optional phrase, exact-input replacement and simplified/traditional examples together. Use `--feature phrases`, `--feature force` or `--feature traditional` to test each independently. It requires the validated macOS Squirrel build and never changes a live profile.

`python3 tests/smoke_config.py --feature mainland --mainland-dir /path/to/generated/package` checks a mainland scheme produced externally by OpenCCGovForRime, including Xiaohe, default traditional output, script switching, native Emoji and fuzzy spelling. It copies the generated scheme and OpenCC resources into an isolated fixture. Mainland templates and personal punctuation are not maintained here.

`python3 tests/smoke_config.py --feature native-emoji` tests native engine Emoji ranking, numeric selection, feedback, composition invalidation and on/off/on switching. Mainland checks exercise the same behaviour in Xiaohe mode. `src/session_options.h` records the validated Mac 3.0.1 session metadata: local normal-Emoji recommendation at byte 45 and local normal-Emoji input at byte 52. The worker pipe accepts `OPEN <mode> [flags]`; omitted or zero means disabled. The mask retains 1 for Unicode Emoji and adds 2 for kaomoji and 4 for text symbols. Both the client and worker reject masks outside 0–7. The native Emoji Plus flag at byte 54 is always cleared, including when the configuration helper receives stale flags. Rime reads the option named by `wetype/emoji_option`, closes the old session and opens a new one when it changes. Raw candidates retain native selection IDs and coverage. New ABI layouts require separate verification; this is Unicode Emoji support, not cloud sticker retrieval or WeChat-internal emoticon rendering.

For paced input and held-delete measurements, add `--timing` to `smoke_rime.py`. Background model loading can change latency, so report the test duration and model state when comparing results.

The real-librime test creates an isolated profile. `--user-profile /path/to/Rime --baseline-schema schema_id` optionally copies static configuration for coexistence checks; it does not copy personal user databases. Successful fixtures are compacted to local `.log` files, while failures remain available for diagnosis. No test restarts the user's Squirrel process.

## Integration boundaries

Rime-side fuzzy spelling is implemented in `src/rime_fuzzy.h` using librime `Script` and `Projection`, not engine fuzzy flags or an additional word dictionary. `speller/algebra` runs on canonical syllables; Xiaohe projection follows it. Plain derive aliases are classified before the layout changes the spelling codes. Bounded complete-syllable paths produce up to three normalized full-pinyin queries in separate sessions; only full-coverage rows are exposed, with coverage remapped to the untouched user input. Candidate feedback uses the branch's native session/ticket/index; ordinary partial selection remains on the original path. Invalid configuration falls back to native candidates and sets `_wetype_fuzzy_error`.

Run `python3 tests/smoke_fuzzy.py --rime-source /path/to/librime` for deterministic Rime algebra, Xiaohe mapping, rule edits, invalid-rule rejection and bounds. Run `python3 tests/smoke_config.py --feature fuzzy` and `--feature fuzzy-flypy` for actual Rime/native selection and toggles. Mainland acceptance also checks filtered traditional fuzzy selection. Existing default-scheme acceptance continues to exercise the native path without fuzzy rules.

`sync_configs` copies distribution YAML/TXT and recursive Lua files independently of engine staging. Conflicting distribution presets are backed up; existing personal patches, dictionaries and text tables are preserved. Each schema inherits `wetype_common:/base` and `wetype_common:/settings`, declares its mode, and applies its normal personal patch last. `wetype_common.custom.yaml` can override shared defaults. `stage` defaults to `<user_data_dir>/wetype/` and generates only `wetype/installation.yaml` for component bindings. In-profile bindings are relative; the Lua loader and native launcher/log lookup resolve them through the actual Rime user directory, independently of the frontend’s working directory. Explicit external runtimes retain absolute bindings. The old generated root configuration is backed up and retired. Default-profile migration renames the old runtime and leaves a compatibility symlink, retaining the same open files and engine locks rather than copying live learning stores. `modes.json` remains the engine ABI catalogue; adding a new input mode requires updating both that catalogue and its scheme, while ordinary configuration edits need only synchronisation and Rime deployment.

The adapter uses Rime's official C++ component and module interfaces. Lua only loads the native module. Candidates retain their session, revision ticket and engine selection identifier so selection feedback cannot be sent to an unrelated query.

`src/rime_user_words.h` reads the ordinary `custom_phrase` namespace with librime's registered `stabledb` and `UserDbValue` interfaces. An explicitly configured `table_translator@custom_phrase` takes ownership of the namespace; its table is not also imported into WeType. A bounded batch is reconciled through the engine's native hot-word getter/setter; deterministic IDs identify the owning table without resetting ordinary learning. Before the first snapshot, removal of a reserved, never-created marker ID waits for the native store to load; an early empty snapshot must not be mistaken for an empty dictionary. Rime's delete notification uses the selected candidate's native session and revision. Hot-word IDs differ from opaque candidate IDs, so exact phrase/code matches use the hot-word removal API; other candidates use the engine's learned-candidate deletion API. User-word imports run during background preparation, not on each keystroke. See [user-word configuration](user-words.md) for persistence and deletion semantics. `smoke_user_words.py` checks native source isolation, storage across worker restarts, actual Rime table parsing in all 12 schemes, native hot-word and learned-candidate deletion, edited sources after reinitialisation, disabled/error fallback and official table-translator coexistence.

Phrase priority is applied only to exact, full-coverage rows already returned by the native engine. A configuration-time index supplies row weights; promoted candidates receive `custom_phrase/initial_quality`, and equal weights retain native ordering. Their original native row indices and tickets survive reordering, including numeric selection and deletion. Ordinary candidates and fuzzy branches retain their existing ordering after the promoted phrases. No extra engine query or text-table read is added to a keystroke.

Configured phrase rows carry Rime's conventional `user_table` type, also used by the official table translator. Identification is separate from promotion, uses the covered source code and phrase, and precedes fuzzy coverage remapping. Each supplied OpenCC simplifier excludes `user_table` through its official `excluded_types` setting. Ordinary native and fuzzy rows retain their types and remain convertible; the native candidate object and feedback identifiers survive unchanged. Tests cover both mainland conversion stages, priority disabled, the official phrase translator and ordinary converted output.

Incompatible text-table codes are isolated before batching, rather than rejecting the whole table and losing its priority index. Counts are exposed through `_wetype_user_words_skipped` and local diagnostics without logging phrase contents. The user-word regression edits a previously valid table, adds incompatible codes, then runs maintenance and recreates sessions in the same librime process; the native host stays alive. This covers frontend deployment as well as full reinitialisation.

The engine and dependent frameworks are copied from a local WeType application. The executable copy is converted into a loadable bundle; algorithm code and model data are not patched. The original application's directory is denied during worker execution.

Text before and after the selection, plus the selected text, is sampled once per composition through the current IMK client. Marked composition text is excluded. Each request is bounded to 256 UTF-16 units and each field to 1,024 UTF-8 bytes; oversized selections are omitted. Unsupported individual ranges remain empty. Before transmission, `src/rime_text_context.h` uses librime's own `SimplifierComponent` and `t2s.json` resource lookup to convert all three fields to simplified Chinese once per composition. It is independent of candidate-output switches and restores its temporary private configuration immediately after initialisation. Missing conversion resources omit context and set `_wetype_context_error`; ordinary candidates remain available. Clipboard and extra engine fields remain empty. The file-only boolean `wetype/surrounding` controls all three fields and defaults to `true`; the translator reads it when the scheme is loaded. Shared defaults live at `settings/surrounding` in `wetype_common.yaml`. No supplied scheme exposes a context switch in its menu, and the former `wetype_surrounding` runtime option is no longer used. The local QUERY protocol accepts optional suffix and selection fields after its existing prefix field for backwards compatibility. Changes to any field reset the native composition, including fuzzy branches. Unsupported clients and secure input fall back to ordinary input. This bridge is specific to the validated Squirrel implementation and needs review when that frontend changes.

`python3 tests/smoke_expressions.py --mainland-dir /path/to/generated/package` validates independent local expression flags,
native candidate feedback and double pinyin, then exercises Rime's official
personal-table backend, literal output, editing, deployment and live option
changes. `tests/session_options.cpp` verifies that all supported masks leave
unrelated, cloud, WeChat and AI configuration fields untouched, and that stale
Plus masks cannot set the native Plus flag. Worker tests reject those masks;
Rime tests confirm that obsolete Plus bindings cannot reactivate it. The
[expression guide](expressions.md) documents the supported features.

## Wubi assistance

The validated Mac 3.0.1 ABI places `_wxime_wubi_config` at session byte 36. Pinyin assistance uses byte 36, wildcard comments byte 39, ordinary comments byte 40 and candidate output byte 41. Both assistance options default to enabled; automatic native emission stays disabled. Rime's four-code forced selection applies only when pinyin assistance is off, allowing longer pinyin input when it is on.

`OPEN <mode> [expression_flags] [wubi_flags]` accepts an optional Wubi mask (1 for pinyin assistance, 2 for code comments, default 3). The existing `QUERY` context fields can be followed by `1` to request an additional hex-encoded comment field per row. Legacy requests retain the original row format. Comments are copied before the native iterator is released; candidate IDs, coverage, tickets and selection feedback retain their existing semantics. The two Rime switches reopen the native session and clear stale composition. Pinyin modes ignore Wubi configuration.

Run `python3 tests/smoke_wubi.py` against the local librime source to verify all three variants in standalone and unified schemes, default options, pinyin selection and commit, remaining-code and wildcard hints, and independent off/on toggles.

## Candidate preferences

`data/lua/wetype_candidate_rules.lua` owns YAML rules, candidate filtering and optional shortcuts. The shared WeType schema registers the filter and processor. Derived schemas that replace `engine/filters` must retain `lua_filter@*wetype_candidate_rules*filter` before script conversion.

The filter identifies genuine native candidates by their WeType type and uses the current `_wetype_mode` and original candidate span. It reuses the original candidate for selection. A shadow can add a pin comment without changing the native ticket, index or feedback path. The adapter does not read or write candidate preference files.

Run `python3 tests/smoke_candidate_preferences.py` to verify live YAML reload, invalid-file fallback, ordered pins, disabled defaults, comment-preserving shortcut writes and native selection in an isolated profile. `tests/smoke_user_words.py` covers native learning deletion.

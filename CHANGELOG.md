# Changelog

## Unreleased

- Use the conventional custom_phrase configuration and existing Rime text tables; remove the separate user-word settings file. Respect an explicitly configured official phrase translator and inherit default key bindings and recogniser rules.

- Manage native WeType user words through Rime's official stabledb text tables, with source-scoped updates and removals, local storage and standard candidate-deletion notifications.

- Add opt-in Rime spelling-algebra fuzzy matching for full pinyin and Xiaohe without enabling engine fuzzy options or adding a word dictionary. Keep rules in wetype_fuzzy.yaml, initially in/ing, and enable the switch in the mainland Xiaohe variant.

- Enable opt-in native WeType Unicode Emoji recommendations through the session's local engine settings, preserving candidate ranking and selection feedback.
- Replace the mainland Xiaohe scheme's OpenCC Emoji mapping with native recommendations; add a reusable YAML option patch and probe --emoji.

- Add the opt-in WeType小鹤大陆 scheme, reusing the native Xiaohe engine with the user's mainland OpenCC conversion resources and Shift-key punctuation preferences.

- Keep all 12 editable scheme YAML files in rime/ and generate interface paths separately in wetype.runtime.yaml.
- Add sync-config for configuration-only updates with backups, plus optional phrase, exact-input replacement and traditional-output examples.

- Discover macOS engine executable and dictionary locations from local metadata, without a version/hash allowlist.
- Preserve all bundled frameworks and resources; validate nested dictionary paths, loader fields and optional vendor checksums.
- Add inspect-macos, actual source metadata and format-2 integrity manifests while retaining format-1 verification.
- Refuse to silently reuse a different engine/resource copy in an existing runtime.


## 0.1.0 — 2026-11-07

Initial macOS source release.

- Offline WeType engine integration through Squirrel and Rime's native component API.
- Full pinyin, seven double-pinyin layouts and three Wubi variants.
- A unified scheme with selectable modes and 11 standalone schemes.
- Native candidates, paging, partial selection and local selection learning.
- Engine-provided syllable boundaries and unchanged uppercase input.
- Bounded preceding-text context, with a per-scheme switch.
- Independent engine resources, learning directories and local `.log` diagnostics.
- Unit, transport, native-engine and real-librime regression checks.

Supports the validated Apple Silicon macOS configuration with WeType 3.0.1 (680), Squirrel 1.1.2 and bundled librime 1.16.0. Proprietary engine resources are supplied locally by the user.

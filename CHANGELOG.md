# Changelog

## Unreleased

- Hide infrequent fuzzy-spelling, expression and Wubi assistance preferences from the menu whilst retaining Rime configuration defaults and option compatibility.

- Delegate mainland Xiaohe generation and distribution to OpenCCGovForRime; retain compatibility with externally installed variants.

- Move the 聯想 / 獨立 editor-context choice out of every supplied input-method menu and into the file-only `wetype/surrounding` setting. Keep context enabled by default and support shared and per-scheme overrides.

- Enable kaomoji and symbol-expression switches by default in the supplied patch, and start the mainland Xiaohe scheme in precise mode whilst retaining its fuzzy toggle.

- Use concise Chinese scheme names and option labels, shared Rime preset inheritance and ordinary personal patches. Keep existing schema IDs and all input features.
- Organise source under `src/` and distribution presets under `data/`. Keep installed engine resources, learning and installation metadata together in `<Rime user directory>/wetype/`; retire the generated root runtime configuration with a backup.
- Relocate the legacy default runtime without copying live learning files, preserving old paths through a compatibility symlink. Support relative in-profile bindings, symlinked profiles and explicit external runtime directories.
- Preserve existing personal patches and text tables during preset synchronisation. Register only the unified scheme on new profiles; retain existing standalone choices and support explicit `--schema` selection.

- Add persistent candidate pinning, blocking and separate restoration shortcuts, scoped to input layout and code, whilst preserving native selection identities and learning for other candidates. Keep native learned-entry deletion separate from reversible display blocking.

- Enable native Wubi pinyin assistance, remaining-code comments and wildcard code hints by default across all three variants and the unified scheme, with independent live switches and selection feedback. Allow longer pinyin composition while assistance is enabled.

- Convert preceding text, following text and selections to simplified Chinese through Rime's OpenCC t2s before passing context to WeType, independently of candidate-output conversion.

- Pass bounded preceding text, following text and selections through the native engine fields, excluding marked composition text. Reuse the same sampling, session isolation and fuzzy-query paths.

- Preserve custom_phrase text through OpenCC using Rime's standard user_table candidate type and excluded_types setting, including both mainland conversion stages and phrases with priority disabled. Ordinary engine output still follows script conversion.

- Fix custom-phrase priority disappearing after deployment when one code falls outside the native input alphabet. Skip those codes individually, preserve the source table and report a local count; cover table edits and redeployment without restarting librime.

- Prioritise exact native custom_phrase matches with the conventional initial_quality setting and row weights, preserving native selection/deletion identifiers and batched input.

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

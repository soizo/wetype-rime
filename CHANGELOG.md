# Changelog

## Unreleased

- Fix multibyte character classes in `/.../` expression keys matching unrelated words. Lua classes are byte-based, so `[蛋卵]` matched any character sharing a byte with those characters — 说 and 试 (both `E8…`) produced 🥚 and 🍇. A class holding multibyte characters now compiles to per-character alternatives; `{n,m}` multiplies every member combination and `?` covers the whole character. ASCII-only classes keep native byte semantics. Negated multibyte classes, multibyte ranges and unbounded `*`/`+`/`-` on such classes are rejected with a message pointing at `{n,m}`. Also fix `?` after a group so `(大利)?` means optional `大利` rather than optional `利`. Add regression coverage for 说/还有/测试 and keep all shipped pattern keys compiling.

- Extend `/.../` expression keys with `|` alternation and `{n,m}` bounded repetition, compiled at table load into native Lua patterns so matching stays `string.find`. Repetition binds to one character, class or `.` (not groups or anchors), with `m ≤ 64` and a 256-pattern expansion cap per key; unbounded multibyte quantifiers are rejected in favour of `{n,m}`. Measured on real Lua 5.4, 100 expanded patterns across 50 candidates add about 0.5 ms per keystroke while literal keys keep zero-overhead hash matching. Cover the extensions, deduplication and error reporting in the acceptance test and document the limits.

- Add hybrid pattern matching to the external expression tables: a plain key stays an exact literal (hash) match, while a key wrapped in `/.../` is a Lua pattern matched with `string.find`, so one pattern can cover many words. Pattern keys are scanned per candidate, separate from the literal hash, to preserve typing speed; malformed patterns are skipped and reported. Update the expression acceptance test for the current filter order and cover pattern matching.

- Stop forcing display preferences in the schemes. Candidate page size now follows Rime's `menu/page_size` default instead of a hardcoded eight, and the fuzzy head window and per-branch fuzzy candidate count become ordinary settings (`wetype/head_candidates`, `wetype/fuzzy_candidates`, both defaulting to three) rather than fixed constants.

- Serve emoji, kaomoji and symbols from hand-maintained editable TSV tables through three Lua filters that match simplified candidate text. Merge personal `.custom.tsv` overlays with hot reload, gate each category by its existing hidden switch, and step aside automatically while the matching native engine category is bound.

- Use Rime's standard table translator for custom phrases in every supplied scheme, allowing multiple exact codes for the same phrase.

- Send semicolons that cannot act as a spelling key to Rime’s punctuation handler. Keep engine errors in diagnostics and allow Return to commit the original input.

- Hide infrequent fuzzy-spelling, expression and Wubi assistance preferences from the menu whilst retaining Rime configuration defaults and option compatibility.

- Delegate mainland Xiaohe generation and distribution to OpenCCGovForRime; retain compatibility with externally installed variants.

- Move the 聯想 / 獨立 editor-context choice out of every supplied input-method menu and into the file-only `wetype/surrounding` setting. Keep context enabled by default and support shared and per-scheme overrides.

- Enable kaomoji and symbol-expression switches by default in the supplied patch, and start the mainland Xiaohe scheme in precise mode whilst retaining its fuzzy toggle.

- Use concise Chinese scheme names and option labels, shared Rime preset inheritance and ordinary personal patches. Keep existing schema IDs and all input features.
- Organise source under `src/` and distribution presets under `data/`. Keep installed engine resources, learning and installation metadata together in `<Rime user directory>/wetype/`; retire the generated root runtime configuration with a backup.
- Relocate the legacy default runtime without copying live learning files, preserving old paths through a compatibility symlink. Support relative in-profile bindings, symlinked profiles and explicit external runtime directories.
- Preserve existing personal patches and text tables during preset synchronisation. Register only the unified scheme on new profiles; retain existing standalone choices and support explicit `--schema` selection.

- Move candidate pinning and blocking to the WeType schemes’ Rime filters. Store ordered rules in a personal YAML file with live reload and leave candidate-management shortcuts disabled by default.

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

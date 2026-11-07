# Changelog

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

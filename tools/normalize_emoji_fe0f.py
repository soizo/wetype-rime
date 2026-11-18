#!/usr/bin/env python3
"""Normalize U+FE0F (emoji presentation selector) in a word<TAB>emoji table.

Adds FE0F after every BMP emoji base that accepts it (both text-default like
☀❤✈ and emoji-default like ⚡⏰⭐), so symbols render as color emoji instead of
text glyphs. Idempotent; leaves ZWJ sequences, keycaps, flags and skin-tone
modifiers intact. Only the emoji column is touched.

Usage: python3 tools/normalize_emoji_fe0f.py [file.tsv ...]
"""
import sys
from pathlib import Path

from emoji import unicode_codes

FE0F = "\ufe0f"
SKIN = range(0x1F3FB, 0x1F400)


def variation_bases() -> set:
    bases = set()
    for key in unicode_codes.EMOJI_DATA:
        if len(key) == 2 and key[1] == FE0F:
            bases.add(key[0])
        elif len(key) == 1 and ord(key) < 0x1F000:
            bases.add(key)
    exclude = set("#*0123456789\u200d\u20e3\ufe0f")
    return {c for c in bases if c not in exclude and ord(c) not in SKIN}


def normalize(value: str, bases: set) -> str:
    out = []
    for i, c in enumerate(value):
        out.append(c)
        if c in bases:
            nxt = value[i + 1] if i + 1 < len(value) else ""
            if nxt == FE0F or (nxt and ord(nxt) in SKIN):
                continue
            out.append(FE0F)
    return "".join(out)


def main(paths: list[str]) -> int:
    bases = variation_bases()
    total = 0
    for p in paths:
        path = Path(p)
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        added = 0
        result = []
        for line in lines:
            body = line.rstrip("\n")
            if body and not body.startswith("#") and "\t" in body:
                word, _, value = body.partition("\t")
                fixed = normalize(value, bases)
                added += fixed.count(FE0F) - value.count(FE0F)
                ending = line[len(body):]
                result.append(word + "\t" + fixed + ending)
            else:
                result.append(line)
        path.write_text("".join(result), encoding="utf-8")
        print(f"{path}: +{added} FE0F")
        total += added
    print(f"total +{total} FE0F")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:] or ["data/wetype_emoji.tsv"]))

#!/usr/bin/env python3
"""Normalize variation selectors in the shipped word<TAB>expression tables.

The emoji table requests colour emoji with U+FE0F after every BMP emoji base
that accepts a variation selector (text-default bases such as ☀❤✈ as well as
emoji-default ones such as ⚡⏰⭐). The symbols table requests text presentation
with U+FE0E after the same bases, so symbols such as ☀ ⚡ ♟ ㊗ stay text glyphs
even when a candidate font would otherwise fall back to a colour emoji font.

Only the expression column is touched. Comments, keys and line endings are
preserved. Execution is idempotent: an existing selector on a base is rewritten
to the table's selector instead of being duplicated, and ZWJ sequences, keycaps,
flags and skin-tone modifiers are left alone.

Usage:
    python3 tools/normalize_presentation.py [--check] [file.tsv ...]
    python3 tools/normalize_presentation.py --selector fe0e my_table.tsv

File names containing "emoji" get FE0F and names containing "symbols" get
FE0E. With no paths, both shipped tables are normalized. --check reports the
files that would change and exits 1 without writing them.
"""
import argparse
import sys
from pathlib import Path

from emoji import unicode_codes

FE0F = "\ufe0f"
FE0E = "\ufe0e"
SELECTORS = {FE0F, FE0E}
SKIN = range(0x1F3FB, 0x1F400)
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PATHS = [ROOT / "data/wetype_emoji.tsv", ROOT / "data/wetype_symbols.tsv"]


def variation_bases() -> set:
    bases = set()
    for key in unicode_codes.EMOJI_DATA:
        if len(key) == 2 and key[1] == FE0F:
            bases.add(key[0])
        elif len(key) == 1 and ord(key) < 0x1F000:
            bases.add(key)
    exclude = set("#*0123456789\u200d\u20e3\ufe0f")
    return {c for c in bases if c not in exclude and ord(c) not in SKIN}


def selector_for(path: Path) -> str | None:
    name = path.name.lower()
    if "symbols" in name:
        return FE0E
    if "emoji" in name:
        return FE0F
    return None


def normalize(value: str, bases: set, selector: str) -> tuple[str, int, int]:
    out = []
    added = replaced = 0
    i = 0
    while i < len(value):
        c = value[i]
        out.append(c)
        if c in bases:
            nxt = value[i + 1] if i + 1 < len(value) else ""
            if nxt in SELECTORS:
                if nxt != selector:
                    replaced += 1
                out.append(selector)
                i += 1  # Skip the original selector; it is replaced, never doubled.
            elif not (nxt and ord(nxt) in SKIN):
                added += 1
                out.append(selector)
        i += 1
    return "".join(out), added, replaced


def process(path: Path, selector: str, bases: set, check: bool) -> tuple[bool, int, int]:
    with path.open(encoding="utf-8", newline="") as handle:
        original = handle.read()
    added = replaced = 0
    result = []
    for line in original.splitlines(keepends=True):
        body = line.rstrip("\n")
        if body and not body.startswith("#") and "\t" in body:
            word, _, value = body.partition("\t")
            fixed, plus, changed = normalize(value, bases, selector)
            added += plus
            replaced += changed
            ending = line[len(body):]
            result.append(word + "\t" + fixed + ending)
        else:
            result.append(line)
    fixed_text = "".join(result)
    changed = fixed_text != original
    if changed and not check:
        path.write_text(fixed_text, encoding="utf-8", newline="")
    state = "would change" if check and changed else ("changed" if changed else "unchanged")
    label = "FE0F" if selector == FE0F else "FE0E"
    print(f"{path}: {state}, +{added} {label}, {replaced} replaced")
    return changed, added, replaced


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="tables to normalize (default: both shipped tables)",
    )
    parser.add_argument(
        "--selector",
        choices=("fe0f", "fe0e"),
        help="force the selector instead of inferring it from the file name",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="report changes without writing, and exit 1 if any file would change",
    )
    args = parser.parse_args(argv)

    bases = variation_bases()
    paths = args.paths or DEFAULT_PATHS
    changed = total_added = total_replaced = 0
    for path in paths:
        selector = (
            FE0E if args.selector == "fe0e"
            else FE0F if args.selector == "fe0f"
            else selector_for(path)
        )
        if selector is None:
            parser.error(f"{path}: cannot infer selector; pass --selector fe0f or fe0e")
        did_change, added, replaced = process(path, selector, bases, args.check)
        changed += did_change
        total_added += added
        total_replaced += replaced
    print(f"total +{total_added} selectors, {total_replaced} replaced, {changed} file(s) changed")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

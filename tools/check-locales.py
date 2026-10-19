#!/usr/bin/env python3
"""Checks data/locale/*.ini against each other and against the keys the code uses.

Fails when a locale misses a key, has an unknown key, a key the code uses is
missing from en-US, or %N placeholders differ between translations.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LOCALES = ["en-US", "zh-CN", "zh-TW"]
LINE = re.compile(r'^([A-Za-z0-9_.]+)="(.*)"\s*$')
USED = re.compile(r'(?:\bT|obs_module_text)\(\s*(?:[a-zA-Z]+ \? )?"([A-Za-z0-9_.]+)"(?: : "([A-Za-z0-9_.]+)")?\s*\)')


def load(name):
    entries = {}
    for number, line in enumerate((ROOT / "data/locale" / f"{name}.ini").read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        match = LINE.match(line)
        if not match:
            sys.exit(f"{name}.ini:{number}: not a key=\"value\" line: {line!r}")
        key, value = match.groups()
        if key in entries:
            sys.exit(f"{name}.ini:{number}: duplicate key {key}")
        entries[key] = value
    return entries


def placeholders(text):
    return sorted(set(re.findall(r"%\d", text)))


def main():
    tables = {name: load(name) for name in LOCALES}
    reference = tables["en-US"]
    errors = []
    for name, table in tables.items():
        for key in sorted(set(reference) - set(table)):
            errors.append(f"{name}: missing {key}")
        for key in sorted(set(table) - set(reference)):
            errors.append(f"{name}: unknown key {key}")
        for key in sorted(set(table) & set(reference)):
            if placeholders(table[key]) != placeholders(reference[key]):
                errors.append(f"{name}: {key} placeholders {placeholders(table[key])} != {placeholders(reference[key])}")
    used = set()
    for source in (ROOT / "src").rglob("*.[ch]pp"):
        for match in USED.finditer(source.read_text(encoding="utf-8")):
            used.update(k for k in match.groups() if k)
    for key in sorted(used - set(reference)):
        errors.append(f"code uses {key}, missing from en-US.ini")
    for key in sorted(set(reference) - used):
        errors.append(f"en-US.ini key {key} is never used")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"locales OK: {len(reference)} keys x {len(LOCALES)} languages, {len(used)} used in code")
    return 0


if __name__ == "__main__":
    sys.exit(main())

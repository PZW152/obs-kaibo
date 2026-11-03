#!/usr/bin/env python3
"""Static design-token guard for the Kaibo dock UI.

Kaibo's colours, spacing, radii and control sizes come from one place:
data/theme/tokens.json. The Qt stylesheet (data/theme/kaibo.qss) may only name
them through @token@ placeholders, and the C++ widgets may only read them through
Theme::color()/Theme::length(). This checker fails when that rule is broken:

  * data/theme/kaibo.qss contains a hard-coded colour instead of @token@;
  * a @token@ used by the stylesheet is not defined in tokens.json;
  * src/ui/*.cpp|*.hpp contains a hard-coded colour (hex string, Qt::white, ...);
  * src/ui/*.cpp reads a colour/length/icon token that tokens.json does not define;
  * tokens.json is structurally broken (missing theme value, unparseable colour).

Documented escape hatch: a line that contains ``check-theme: allow`` skips the
next line (or itself) for the colour and token-name rules, so a genuinely
unavoidable fallback can be annotated in place. Keep the allowlist tiny and
explain why in the comment.

Usage:
    python tools/check-theme.py            # from the repo root
    python tools/check-theme.py --root .   # explicit repo root

Exit code 0 = clean, 1 = violations. Standard library only; Python 3.7+.
"""
import argparse
import json
import pathlib
import re
import sys

COLOR_TOKENS = ("color",)
LENGTH_FAMILIES = ("spacing", "radius", "size")
UI_SOURCES = ("src/ui",)
QSS_PATH = pathlib.Path("data/theme/kaibo.qss")
TOKENS_PATH = pathlib.Path("data/theme/tokens.json")
ALLOW_MARKER = "check-theme: allow"

# Colour literal in the Qt stylesheet: #rgb/#rrggbb/#rrggbbaa or rgb()/rgba().
QSS_HEX = re.compile(r"#[0-9a-fA-F]{3,8}\b")
QSS_FUNC = re.compile(r"\brgba?\s*\(")
QSS_NAMED = re.compile(
    r"(?<![\w-])(black|white|red|green|blue|yellow|orange|purple|cyan|magenta|"
    r"gray|grey|pink|brown|navy|teal)(?![\w-])",
    re.IGNORECASE,
)
QSS_PLACEHOLDER = re.compile(r"@([A-Za-z0-9_][A-Za-z0-9_-]*)@")

# Colour literals in C++/Qt code.
CPP_HEX = re.compile(r'"(#[0-9a-fA-F]{3,8})"')
CPP_QT_COLOR = re.compile(
    r"\bQt::(white|black|red|green|blue|yellow|orange|purple|cyan|magenta|"
    r"gray|grey|darkRed|darkGreen|darkBlue|darkGray|darkGrey|lightGray|lightGrey|"
    r"transparent|color0|color1)\b"
)
CPP_ALLOWED_QT = {"transparent", "color0", "color1"}

# Theme accessors whose argument must be a defined token.
CPP_COLOR_TOKEN = re.compile(r'\.color\(\s*(?:QStringLiteral\()?"([A-Za-z0-9_][A-Za-z0-9_-]*)"')
CPP_LENGTH_TOKEN = re.compile(r'\.length\(\s*(?:QStringLiteral\()?"([A-Za-z0-9_][A-Za-z0-9_-]*)"')
CPP_ICON_TOKEN = re.compile(
    r'\.icon\(\s*(?:QStringLiteral\()?"[^"]+"\s*,\s*(?:QStringLiteral\()?"([A-Za-z0-9_][A-Za-z0-9_-]*)"'
)

HEX3 = re.compile(r"^#([0-9a-fA-F]{3})$")
HEX4 = re.compile(r"^#([0-9a-fA-F]{4})$")
HEX6 = re.compile(r"^#([0-9a-fA-F]{6})$")
HEX8 = re.compile(r"^#([0-9a-fA-F]{8})$")
RGB = re.compile(
    r"^rgba?\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*(?:,\s*([0-9.]+)\s*)?\)$"
)


class Report:
    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, where, message):
        self.errors.append(f"{where}: {message}")

    def warn(self, message):
        self.warnings.append(message)


def strip_qss_comments(text):
    """Blank out comments while keeping newlines so line numbers stay right."""
    return re.sub(
        r"/\*.*?\*/",
        lambda match: "\n" * match.group(0).count("\n"),
        text,
        flags=re.S,
    )


def load_tokens(root, report):
    path = root / TOKENS_PATH
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        report.error(path.as_posix(), f"cannot read: {error}")
        return {}, {}
    except json.JSONDecodeError as error:
        report.error(path.as_posix(), f"invalid JSON: {error}")
        return {}, {}

    colors = {}
    for token in data.get("color", {}).get("tokens", []):
        name = token.get("name")
        value = token.get("value")
        if not isinstance(name, str) or not name:
            report.error(path.as_posix(), f"colour token without a name: {token!r}")
            continue
        if isinstance(value, str):
            pair = {"dark": value, "light": value}
        elif isinstance(value, dict):
            pair = {"dark": value.get("dark"), "light": value.get("light")}
        else:
            report.error(path.as_posix(), f"colour token {name}: bad value {value!r}")
            continue
        for theme in ("dark", "light"):
            raw = pair[theme]
            if not isinstance(raw, str) or not parse_color(raw):
                report.error(
                    path.as_posix(),
                    f"colour token {name}: {theme} value {raw!r} is not a CSS colour",
                )
        colors[name] = pair

    lengths = {}
    for family in LENGTH_FAMILIES:
        for token in data.get(family, {}).get("tokens", []):
            name = token.get("name")
            value = token.get("value")
            if not isinstance(name, str) or not name:
                report.error(path.as_posix(), f"{family} token without a name: {token!r}")
                continue
            if not isinstance(value, str) or not re.match(r"^\d+(?:\.\d+)?px$", value):
                report.error(path.as_posix(), f"{family} token {name}: value {value!r} is not a px length")
                continue
            lengths[name] = value
    return colors, lengths


def parse_color(value):
    value = value.strip()
    for pattern in (HEX3, HEX4, HEX6, HEX8):
        match = pattern.match(value)
        if match:
            return True
    match = RGB.match(value)
    if match:
        for part in match.groups()[:3]:
            if int(part) > 255:
                return False
        if match.group(4) is not None:
            try:
                if not 0.0 <= float(match.group(4)) <= 1.0:
                    return False
            except ValueError:
                return False
        return True
    return False


def check_qss(root, colors, lengths, report):
    path = root / QSS_PATH
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        report.error(path.as_posix(), f"cannot read: {error}")
        return set()
    clean = strip_qss_comments(text)
    used = set()
    allow_next = False
    for number, line in enumerate(clean.splitlines(), 1):
        if ALLOW_MARKER in line:
            allow_next = True
            continue
        if allow_next:  # the marker on the line above allows this one line
            allow_next = False
            continue
        for match in QSS_PLACEHOLDER.finditer(line):
            name = match.group(1)
            used.add(name)
            if name not in colors and name not in lengths:
                report.error(
                    path.as_posix(),
                    f"{number}: @{name}@ is not defined in {TOKENS_PATH.as_posix()}",
                )
        for match in QSS_HEX.finditer(line):
            report.error(
                path.as_posix(),
                f"{number}: hard-coded colour {match.group(0)!r} — use a @token@ from tokens.json",
            )
        for match in QSS_FUNC.finditer(line):
            report.error(
                path.as_posix(),
                f"{number}: hard-coded {match.group(0)!r} colour — use a @token@ from tokens.json",
            )
        for match in QSS_NAMED.finditer(line):
            report.error(
                path.as_posix(),
                f"{number}: hard-coded colour {match.group(0)!r} — use a @token@ from tokens.json",
            )
    return used


def check_source(root, path, colors, lengths, report, used_tokens):
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        report.error(path.as_posix(), f"cannot read: {error}")
        return
    where = path.relative_to(root).as_posix()
    allow_next = False
    for number, line in enumerate(lines, 1):
        if ALLOW_MARKER in line:
            allow_next = True
            continue
        if allow_next:  # the marker on the line above allows this one line
            allow_next = False
            continue
        for match in CPP_HEX.finditer(line):
            report.error(
                where,
                f"{number}: hard-coded colour {match.group(1)!r} — read it from tokens.json via Theme::color()",
            )
        for match in CPP_QT_COLOR.finditer(line):
            if match.group(1) in CPP_ALLOWED_QT:
                continue
            report.error(
                where,
                f"{number}: hard-coded colour Qt::{match.group(1)} — read it from tokens.json via Theme::color()",
            )
        for match in CPP_COLOR_TOKEN.finditer(line):
            name = match.group(1)
            used_tokens.add(name)
            if name not in colors:
                report.error(where, f"{number}: theme().color(\"{name}\") is not a colour token in tokens.json")
        for match in CPP_LENGTH_TOKEN.finditer(line):
            name = match.group(1)
            used_tokens.add(name)
            if name not in lengths:
                report.error(where, f"{number}: theme().length(\"{name}\") is not a length token in tokens.json")
        for match in CPP_ICON_TOKEN.finditer(line):
            name = match.group(1)
            used_tokens.add(name)
            if name not in colors:
                report.error(where, f"{number}: theme().icon(..., \"{name}\") is not a colour token in tokens.json")


def main(argv):
    parser = argparse.ArgumentParser(description="Check that the Kaibo UI only uses design tokens.")
    parser.add_argument("--root", default=None, help="repository root (defaults to this script's parent)")
    args = parser.parse_args(argv)
    root = pathlib.Path(args.root).resolve() if args.root else pathlib.Path(__file__).resolve().parent.parent

    report = Report()
    colors, lengths = load_tokens(root, report)

    print(f"theme check: {TOKENS_PATH.as_posix()} — {len(colors)} colours, {len(lengths)} lengths")
    qss_tokens = check_qss(root, colors, lengths, report)
    print(f"theme check: {QSS_PATH.as_posix()} — {len(qss_tokens)} distinct @tokens@ used")

    used_tokens = set(qss_tokens)
    sources = []
    for folder in UI_SOURCES:
        directory = root / folder
        if not directory.is_dir():
            report.error(folder, "UI source directory is missing")
            continue
        for path in sorted(list(directory.glob("*.cpp")) + list(directory.glob("*.hpp"))):
            sources.append(path)
            check_source(root, path, colors, lengths, report, used_tokens)
    print(f"theme check: {len(sources)} C++ files scanned in {', '.join(UI_SOURCES)}")

    for name in sorted(set(colors) - used_tokens - {"transparent"}):
        report.warn(f"colour token {name} is defined but never referenced by the dock UI")

    for warning in report.warnings:
        print(f"theme check: warning: {warning}")
    if report.errors:
        print(f"theme check FAILED: {len(report.errors)} problem(s)")
        for error in report.errors:
            print(f"  {error}")
        return 1
    print(f"theme check PASS: no hard-coded colours, every @token@ resolves ({len(sources)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

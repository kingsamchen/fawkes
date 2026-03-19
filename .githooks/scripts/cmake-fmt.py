#!/usr/bin/env python3
"""cmake-fmt – an opinionated CMake formatter (single-file edition).

Usage:
    python cmake-fmt.py [OPTIONS] [FILES...]
    python cmake-fmt.py --help
"""

from __future__ import annotations

__version__ = "0.1.0"

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Union


# ═══════════════════════════════════════════════════════════════════════════
# Parser  –  scan CMake source into FileElements and tokenize arguments
# ═══════════════════════════════════════════════════════════════════════════


@dataclass
class Command:
    name: str
    raw_args: str
    trailing_comment: str = ""


@dataclass
class Comment:
    text: str


@dataclass
class BlankLines:
    count: int = 1


FileElement = Union[Command, Comment, BlankLines]


@dataclass
class ArgItem:
    value: str
    comment: str = ""


@dataclass
class ArgBlankLine:
    pass


ArgElement = Union[ArgItem, ArgBlankLine]


class CMakeParser:
    def __init__(self, text: str):
        self.text = text
        self.pos = 0
        self.end = len(text)

    def parse(self) -> list[FileElement]:
        elements: list[FileElement] = []
        while self.pos < self.end:
            self._skip_hws()
            if self.pos >= self.end:
                break
            ch = self.text[self.pos]
            if ch == "\n":
                n = self._consume_blank_lines()
                elements.append(BlankLines(count=n))
            elif ch == "#":
                line = self._read_to_eol()
                elements.append(Comment(text=line.rstrip()))
                self._skip_newline()
            elif ch.isalpha() or ch == "_":
                cmd = self._try_read_command()
                if cmd:
                    elements.append(cmd)
            else:
                self._read_to_eol()
                self._skip_newline()
        return elements

    def _skip_hws(self):
        while self.pos < self.end and self.text[self.pos] in " \t":
            self.pos += 1

    def _skip_newline(self):
        if self.pos < self.end and self.text[self.pos] == "\n":
            self.pos += 1

    def _read_to_eol(self) -> str:
        start = self.pos
        while self.pos < self.end and self.text[self.pos] != "\n":
            self.pos += 1
        return self.text[start : self.pos]

    def _consume_blank_lines(self) -> int:
        count = 0
        while self.pos < self.end and self.text[self.pos] == "\n":
            count += 1
            self.pos += 1
            self._skip_hws()
        return count

    def _try_read_command(self) -> Optional[Command]:
        start = self.pos
        while self.pos < self.end and (
            self.text[self.pos].isalnum() or self.text[self.pos] == "_"
        ):
            self.pos += 1
        name = self.text[start : self.pos]
        self._skip_hws()
        if self.pos >= self.end or self.text[self.pos] != "(":
            self._read_to_eol()
            self._skip_newline()
            return None
        self.pos += 1
        args_start = self.pos
        depth = 1
        while self.pos < self.end and depth > 0:
            ch = self.text[self.pos]
            if ch == "(":
                depth += 1
                self.pos += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    break
                self.pos += 1
            elif ch == '"':
                self._skip_string()
            elif ch == "#":
                while self.pos < self.end and self.text[self.pos] != "\n":
                    self.pos += 1
            elif ch == "[" and self._at_bracket():
                self._skip_bracket()
            else:
                self.pos += 1
        raw_args = self.text[args_start : self.pos]
        if self.pos < self.end and self.text[self.pos] == ")":
            self.pos += 1
        self._skip_hws()
        trailing = ""
        if self.pos < self.end and self.text[self.pos] == "#":
            trailing = self._read_to_eol().rstrip()
        self._skip_newline()
        return Command(name=name, raw_args=raw_args, trailing_comment=trailing)

    def _skip_string(self):
        self.pos += 1
        while self.pos < self.end:
            if self.text[self.pos] == "\\" and self.pos + 1 < self.end:
                self.pos += 2
            elif self.text[self.pos] == '"':
                self.pos += 1
                return
            else:
                self.pos += 1

    def _at_bracket(self) -> bool:
        if self.pos >= self.end or self.text[self.pos] != "[":
            return False
        i = self.pos + 1
        while i < self.end and self.text[i] == "=":
            i += 1
        return i < self.end and self.text[i] == "["

    def _skip_bracket(self):
        self.pos += 1
        eq = 0
        while self.pos < self.end and self.text[self.pos] == "=":
            self.pos += 1
            eq += 1
        self.pos += 1
        close = "]" + "=" * eq + "]"
        n = len(close)
        while self.pos < self.end:
            if self.text[self.pos : self.pos + n] == close:
                self.pos += n
                return
            self.pos += 1


# -- argument tokenizer -----------------------------------------------------


def _tokenize_args(raw: str) -> list[ArgElement]:
    lines = _split_arg_lines(raw)
    result: list[ArgElement] = []
    for line in lines:
        stripped = line.strip()
        if not stripped:
            if result and not isinstance(result[-1], ArgBlankLine):
                result.append(ArgBlankLine())
            continue
        args, comment = _parse_line(stripped)
        for i, arg in enumerate(args):
            if i == len(args) - 1 and comment:
                result.append(ArgItem(value=arg, comment=comment))
            else:
                result.append(ArgItem(value=arg))
        if not args and comment:
            result.append(ArgItem(value="", comment=comment))
    while result and isinstance(result[-1], ArgBlankLine):
        result.pop()
    while result and isinstance(result[0], ArgBlankLine):
        result.pop(0)
    return result


def _split_arg_lines(raw: str) -> list[str]:
    lines: list[str] = []
    current: list[str] = []
    pos = 0
    end = len(raw)
    while pos < end:
        ch = raw[pos]
        if ch == "\n":
            lines.append("".join(current))
            current = []
            pos += 1
        elif ch == '"':
            start = pos
            pos += 1
            while pos < end:
                if raw[pos] == "\\" and pos + 1 < end:
                    pos += 2
                elif raw[pos] == '"':
                    pos += 1
                    break
                else:
                    pos += 1
            current.append(raw[start:pos])
        elif ch == "[" and _at_bracket(raw, pos, end):
            start = pos
            pos = _end_bracket(raw, pos, end)
            current.append(raw[start:pos])
        else:
            current.append(ch)
            pos += 1
    if current:
        lines.append("".join(current))
    return lines


def _parse_line(line: str) -> tuple[list[str], str]:
    args: list[str] = []
    comment = ""
    pos = 0
    end = len(line)
    while pos < end:
        if line[pos] in " \t":
            pos += 1
            continue
        if line[pos] == "#":
            comment = line[pos:]
            break
        start = pos
        while pos < end and line[pos] not in " \t#":
            if line[pos] == '"':
                pos += 1
                while pos < end:
                    if line[pos] == "\\" and pos + 1 < end:
                        pos += 2
                    elif line[pos] == '"':
                        pos += 1
                        break
                    else:
                        pos += 1
            else:
                pos += 1
        if pos > start:
            args.append(line[start:pos])
    return args, comment


def _at_bracket(text: str, pos: int, end: int) -> bool:
    if pos >= end or text[pos] != "[":
        return False
    i = pos + 1
    while i < end and text[i] == "=":
        i += 1
    return i < end and text[i] == "["


def _end_bracket(text: str, pos: int, end: int) -> int:
    pos += 1
    eq = 0
    while pos < end and text[pos] == "=":
        pos += 1
        eq += 1
    pos += 1
    close = "]" + "=" * eq + "]"
    n = len(close)
    while pos < end:
        if text[pos : pos + n] == close:
            return pos + n
        pos += 1
    return pos


# ═══════════════════════════════════════════════════════════════════════════
# Formatter  –  apply formatting rules to parsed elements
# ═══════════════════════════════════════════════════════════════════════════

BLOCK_OPENERS = frozenset({"if", "foreach", "while", "function", "macro", "block"})
BLOCK_CLOSERS = frozenset(
    {"endif", "endforeach", "endwhile", "endfunction", "endmacro", "endblock"}
)
BLOCK_MID = frozenset({"elseif", "else"})

TARGET_COMMANDS = frozenset(
    {
        "target_sources",
        "target_link_libraries",
        "target_include_directories",
        "target_compile_definitions",
        "target_compile_options",
        "target_compile_features",
        "target_link_options",
        "target_link_directories",
        "target_precompile_headers",
    }
)

_VISIBILITY_KEYWORDS = frozenset({"PRIVATE", "PUBLIC", "INTERFACE"})

COMMAND_KEYWORDS: dict[str, frozenset[str]] = {
    "target_sources": _VISIBILITY_KEYWORDS,
    "target_link_libraries": _VISIBILITY_KEYWORDS,
    "target_include_directories": _VISIBILITY_KEYWORDS,
    "target_compile_definitions": _VISIBILITY_KEYWORDS,
    "target_compile_options": _VISIBILITY_KEYWORDS,
    "target_compile_features": _VISIBILITY_KEYWORDS,
    "target_link_options": _VISIBILITY_KEYWORDS,
    "target_link_directories": _VISIBILITY_KEYWORDS,
    "target_precompile_headers": _VISIBILITY_KEYWORDS | {"REUSE_FROM"},
}


class CMakeFormatter:
    def __init__(self, indent_size: int = 2):
        self.indent_size = indent_size
        self.depth = 0

    def format(self, elements: list[FileElement]) -> str:
        parts: list[str] = []
        for elem in elements:
            if isinstance(elem, BlankLines):
                parts.append("")
            elif isinstance(elem, Comment):
                parts.append(f"{self._base_indent()}{elem.text}")
            elif isinstance(elem, Command):
                parts.append(self._format_command(elem))
        text = "\n".join(parts)
        while "\n\n\n" in text:
            text = text.replace("\n\n\n", "\n\n")
        text = text.strip("\n") + "\n"
        return text

    def _base_indent(self) -> str:
        return " " * (self.indent_size * self.depth)

    def _format_command(self, cmd: Command) -> str:
        lo = cmd.name.lower()
        if lo in BLOCK_CLOSERS:
            self.depth = max(0, self.depth - 1)
        elif lo in BLOCK_MID:
            self.depth = max(0, self.depth - 1)
        result = (
            self._format_target(cmd)
            if lo in TARGET_COMMANDS
            else self._format_regular(cmd)
        )
        if lo in BLOCK_OPENERS or lo in BLOCK_MID:
            self.depth += 1
        return result

    # -- regular commands ----------------------------------------------------

    def _format_regular(self, cmd: Command) -> str:
        tokens = _tokenize_args(cmd.raw_args)
        arg_values = [t.value for t in tokens if isinstance(t, ArgItem) and t.value]
        base = self._base_indent()
        name = cmd.name.lower()
        i1 = " " * self.indent_size

        if not arg_values:
            line = f"{base}{name}()"
            if cmd.trailing_comment:
                line += f" {cmd.trailing_comment}"
            return line

        is_multiline = "\n" in cmd.raw_args and len(arg_values) > 1
        if not is_multiline:
            line = f"{base}{name}({' '.join(arg_values)})"
            if cmd.trailing_comment:
                line += f" {cmd.trailing_comment}"
            return line

        parsed_lines = _parse_raw_arg_lines(cmd.raw_args)
        first_args, first_cmt = parsed_lines[0]
        head = f"{base}{name}({first_args}"
        if first_cmt:
            head += f" {first_cmt}"
        lines: list[str] = [head]
        for args_str, cmt in parsed_lines[1:]:
            if not args_str and not cmt:
                lines.append("")
            elif args_str and cmt:
                lines.append(f"{base}{i1}{args_str} {cmt}")
            elif args_str:
                lines.append(f"{base}{i1}{args_str}")
            else:
                lines.append(f"{base}{i1}{cmt}")
        lines.append(f"{base})")
        result = "\n".join(lines)
        if cmd.trailing_comment:
            result += f" {cmd.trailing_comment}"
        return result

    # -- target_* commands ---------------------------------------------------

    def _format_target(self, cmd: Command) -> str:
        tokens = _tokenize_args(cmd.raw_args)
        if not tokens:
            return f"{self._base_indent()}{cmd.name.lower()}()"

        base = self._base_indent()
        name = cmd.name.lower()
        keywords = COMMAND_KEYWORDS.get(name, _VISIBILITY_KEYWORDS)
        i1 = " " * self.indent_size
        i2 = " " * (self.indent_size * 2)

        first_arg = ""
        idx = 0
        while idx < len(tokens):
            tok = tokens[idx]
            if isinstance(tok, ArgBlankLine):
                idx += 1
                continue
            if isinstance(tok, ArgItem):
                if tok.value in keywords:
                    break
                if tok.value:
                    first_arg = tok.value
                    idx += 1
                    break
            idx += 1

        remaining = tokens[idx:]
        has_body = any(
            isinstance(t, ArgItem) and (t.value or t.comment) for t in remaining
        )
        if not has_body:
            return f"{base}{name}({first_arg})" if first_arg else f"{base}{name}()"

        lines: list[str] = [f"{base}{name}({first_arg}"]
        sections = _parse_sections(remaining, keywords)

        for sec_idx, section in enumerate(sections):
            kw = section["keyword"]
            chunks = section["chunks"]

            if sec_idx > 0:
                lines.append("")
            if kw:
                lines.append(f"{base}{i1}{kw}")

            item_indent = f"{base}{i2}" if kw else f"{base}{i1}"
            for chunk in chunks:
                if chunk is None:
                    lines.append("")
                elif isinstance(chunk, str):
                    lines.append(f"{item_indent}{chunk}")
                elif isinstance(chunk, list):
                    sorted_units = _sort_group(chunk)
                    for unit in sorted_units:
                        if _is_genexpr_unit(unit):
                            lines.extend(
                                _format_genexpr_unit(
                                    unit, item_indent, " " * self.indent_size
                                )
                            )
                        else:
                            for val, cmt in unit:
                                if val and cmt:
                                    lines.append(f"{item_indent}{val} {cmt}")
                                elif val:
                                    lines.append(f"{item_indent}{val}")
                                elif cmt:
                                    lines.append(f"{item_indent}{cmt}")

        lines.append(f"{base})")
        result = "\n".join(lines)
        if cmd.trailing_comment:
            result += f" {cmd.trailing_comment}"
        return result


# -- regular-command line-grouping helper ------------------------------------


def _parse_raw_arg_lines(raw_args: str) -> list[tuple[str, str]]:
    result: list[tuple[str, str]] = []
    for raw_line in raw_args.split("\n"):
        stripped = raw_line.strip()
        if not stripped:
            result.append(("", ""))
            continue
        args, comment = _parse_line(stripped)
        result.append((" ".join(args), comment))
    while result and result[-1] == ("", ""):
        result.pop()
    while result and result[0] == ("", ""):
        result.pop(0)
    return result


# -- section parsing ---------------------------------------------------------

SectionGroup = list[tuple[str, str]]


def _parse_sections(
    tokens: list[ArgElement],
    keywords: frozenset[str] = _VISIBILITY_KEYWORDS,
) -> list[dict]:
    sections: list[dict] = []
    current: dict | None = None
    group: SectionGroup = []

    def _flush_group():
        nonlocal group
        if current is not None and group:
            current["chunks"].append(group)
            group = []

    for tok in tokens:
        if isinstance(tok, ArgBlankLine):
            _flush_group()
            if current is not None:
                current["chunks"].append(None)
            continue

        assert isinstance(tok, ArgItem)

        if tok.value in keywords:
            _flush_group()
            if current is not None:
                sections.append(current)
            current = {"keyword": tok.value, "chunks": []}
        elif not tok.value and tok.comment:
            _flush_group()
            if current is None:
                current = {"keyword": None, "chunks": []}
            current["chunks"].append(tok.comment)
        elif tok.value:
            if current is None:
                current = {"keyword": None, "chunks": []}
            group.append((tok.value, tok.comment))

    _flush_group()
    if current is not None:
        sections.append(current)

    for section in sections:
        while section["chunks"] and section["chunks"][-1] is None:
            section["chunks"].pop()

    return sections


# -- sorting -----------------------------------------------------------------


def _sort_key(unit: list[tuple[str, str]]) -> str:
    for val, _ in unit:
        if val:
            return val.lower()
    return "\xff"


def _sort_group(group: SectionGroup) -> list[list[tuple[str, str]]]:
    units = _make_sort_units(group)
    units.sort(key=_sort_key)
    return units


def _make_sort_units(
    group: SectionGroup,
) -> list[list[tuple[str, str]]]:
    units: list[list[tuple[str, str]]] = []
    i = 0
    pending_comments: list[tuple[str, str]] = []

    while i < len(group):
        val, cmt = group[i]

        if not val and cmt:
            pending_comments.append(("", cmt))
            i += 1
            continue

        if val and "$<" in val:
            depth = val.count("<") - val.count(">")
            if depth > 0:
                unit = pending_comments + [(val, cmt)]
                pending_comments = []
                i += 1
                while i < len(group) and depth > 0:
                    v2, c2 = group[i]
                    unit.append((v2, c2))
                    depth += v2.count("<") - v2.count(">")
                    i += 1
                units.append(unit)
                continue

        unit = pending_comments + [(val, cmt)]
        pending_comments = []
        units.append(unit)
        i += 1

    if pending_comments:
        if units:
            units[-1].extend(pending_comments)
        else:
            units.append(pending_comments)

    return units


# -- generator-expression helpers --------------------------------------------


def _is_genexpr_unit(unit: list[tuple[str, str]]) -> bool:
    if not unit:
        return False
    first_val = unit[0][0]
    if not first_val or "$<" not in first_val:
        return False
    if len(unit) == 1:
        return (
            first_val.startswith("$<")
            and first_val.endswith(">")
            and first_val.count("<") == first_val.count(">")
        )
    return first_val.count("<") > first_val.count(">") and unit[-1][0] == ">"


def _parse_complete_genexpr(val: str) -> tuple[str, list[str]] | None:
    if not val.startswith("$<") or not val.endswith(">"):
        return None
    inner = val[:-1]
    depth = 0
    last_colon = -1
    i = 0
    while i < len(inner):
        if inner[i : i + 2] == "$<":
            depth += 1
            i += 2
            continue
        if inner[i] == ">":
            depth -= 1
            i += 1
            continue
        if inner[i] == ":" and depth == 1:
            last_colon = i
        i += 1
    if last_colon == -1:
        return None
    prefix = val[:last_colon]
    value_str = val[last_colon + 1 : -1]
    values = _split_genexpr_values(value_str)
    return prefix, values


def _split_genexpr_values(value_str: str) -> list[str]:
    if not value_str:
        return []
    values: list[str] = []
    depth = 0
    start = 0
    i = 0
    while i < len(value_str):
        if value_str[i : i + 2] == "$<":
            depth += 1
            i += 2
            continue
        if value_str[i] == ">" and depth > 0:
            depth -= 1
            i += 1
            continue
        if value_str[i] == ";" and depth == 0:
            part = value_str[start:i]
            if part:
                values.append(part)
            start = i + 1
            i += 1
            continue
        i += 1
    tail = value_str[start:]
    if tail:
        values.append(tail)
    return values


def _format_genexpr_unit(
    unit: list[tuple[str, str]],
    item_indent: str,
    indent_step: str,
) -> list[str]:
    first_val = unit[0][0]
    sub = item_indent + indent_step

    if len(unit) == 1:
        parsed = _parse_complete_genexpr(first_val)
        if not parsed or len(parsed[1]) <= 1:
            cmt = unit[0][1]
            if cmt:
                return [f"{item_indent}{first_val} {cmt}"]
            return [f"{item_indent}{first_val}"]
        prefix, values = parsed
        values.sort(key=str.lower)
        lines = [f"{item_indent}{prefix}:"]
        for v in values:
            lines.append(f"{sub}{v}")
        lines.append(f"{item_indent}>")
        return lines

    prefix = first_val
    middle = unit[1:-1]
    value_items = [(v, c) for v, c in middle if v]

    if len(value_items) >= 2:
        value_items.sort(key=lambda vc: vc[0].lower())

    lines = [f"{item_indent}{prefix}"]
    for v, c in value_items:
        if v and c:
            lines.append(f"{sub}{v} {c}")
        elif v:
            lines.append(f"{sub}{v}")
    for v, c in middle:
        if not v and c:
            lines.append(f"{sub}{c}")
    lines.append(f"{item_indent}>")
    return lines


# ═══════════════════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════════════════


def format_text(text: str, indent: int = 2) -> str:
    elements = CMakeParser(text).parse()
    formatter = CMakeFormatter(indent_size=indent)
    return formatter.format(elements)


def _process_file(path: Path, args: argparse.Namespace) -> int:
    text = path.read_text(encoding="utf-8")
    formatted = format_text(text, args.indent)

    if args.check:
        if text != formatted:
            print(f"would reformat: {path}", file=sys.stderr)
            return 1
        return 0

    if args.diff:
        if text != formatted:
            import difflib

            diff = difflib.unified_diff(
                text.splitlines(keepends=True),
                formatted.splitlines(keepends=True),
                fromfile=str(path),
                tofile=str(path),
            )
            if _should_color(args):
                sys.stdout.writelines(_colorize_diff(diff))
            else:
                sys.stdout.writelines(diff)
            return 1
        return 0

    if args.in_place:
        if text != formatted:
            path.write_text(formatted, encoding="utf-8")
            print(f"formatted: {path}", file=sys.stderr)
        return 0

    sys.stdout.write(formatted)
    return 0


def _collect_files(sources: list[str]) -> list[Path]:
    files: list[Path] = []
    for src in sources:
        p = Path(src)
        if p.is_dir():
            files.extend(sorted(p.rglob("CMakeLists.txt")))
            files.extend(sorted(p.rglob("*.cmake")))
        else:
            files.append(p)
    return files


def _should_color(args: argparse.Namespace) -> bool:
    mode = getattr(args, "color", "auto")
    if mode == "always":
        return True
    if mode == "never":
        return False
    return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()


_RESET = "\033[0m"
_RED = "\033[31m"
_GREEN = "\033[32m"
_CYAN = "\033[36m"
_BOLD = "\033[1m"


def _colorize_diff(lines):
    for line in lines:
        if line.startswith("---") or line.startswith("+++"):
            yield f"{_BOLD}{line}{_RESET}"
        elif line.startswith("@@"):
            yield f"{_CYAN}{line}{_RESET}"
        elif line.startswith("+"):
            yield f"{_GREEN}{line}{_RESET}"
        elif line.startswith("-"):
            yield f"{_RED}{line}{_RESET}"
        else:
            yield line


def main() -> int:
    ap = argparse.ArgumentParser(
        prog="cmake-fmt",
        description="Format CMake files with opinionated rules for target_* commands.",
    )
    ap.add_argument("files", nargs="*", help="files or directories (use - for stdin)")
    ap.add_argument("--indent", type=int, default=2, help="indent width (default: 2)")
    ap.add_argument("-i", "--in-place", action="store_true", help="edit files in-place")
    ap.add_argument(
        "--check",
        action="store_true",
        help="exit 1 if any file would be reformatted",
    )
    ap.add_argument("--diff", action="store_true", help="print unified diff")
    ap.add_argument(
        "--color",
        choices=["auto", "always", "never"],
        default="auto",
        help="colorize diff output (default: auto)",
    )
    ap.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    args = ap.parse_args()

    if not args.files or args.files == ["-"]:
        text = sys.stdin.read()
        sys.stdout.write(format_text(text, args.indent))
        return 0

    rc = 0
    for path in _collect_files(args.files):
        rc |= _process_file(path, args)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())

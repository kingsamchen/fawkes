#!/usr/bin/env python3

"""
Powered by AI.
Remove substrings of the form:
  -include <path ... cmake_pch.hxx>
from the "command" field of each entry in a compile_commands.json array.
"""

from __future__ import annotations
import json
import re
import sys
from pathlib import Path
from typing import Tuple

# Regex: match "-include" followed by whitespace and a path that ends with "cmake_pch.hxx".
# Path can be:
#  - double-quoted:  -include "/some/path/.../cmake_pch.hxx"
#  - single-quoted:  -include '/some/path/.../cmake_pch.hxx'
#  - unquoted (no spaces): -include /some/path/.../cmake_pch.hxx
# We allow optional trailing punctuation (comma) when present inside arguments arrays, but we remove only the matched substring;
# final whitespace cleanup will remove extra spaces/commas if desired.
PATTERN = re.compile(
    r'''-include                           # literal -include
        \s+                                # at least one whitespace
        (?:                                # path alternatives:
            "(?:[^"\\]|\\.)*cmake_pch\.hxx"   # double-quoted string containing cmake_pch.hxx
          | '(?:[^'\\]|\\.)*cmake_pch\.hxx'   # single-quoted string containing cmake_pch.hxx
          | \S*cmake_pch\.hxx\b               # unquoted token ending with cmake_pch.hxx
        )
    ''',
    re.IGNORECASE | re.VERBOSE
)


def clean_command_string(cmd: str) -> Tuple[str, int]:
    """
    Remove all occurrences matching PATTERN from cmd, normalize whitespace,
    and return (cleaned_command, number_of_removals).
    """
    # Count matches before substitution
    matches = list(PATTERN.finditer(cmd))
    count = len(matches)
    if count == 0:
        return cmd, 0

    cleaned = PATTERN.sub('', cmd)
    # Normalize whitespace: collapse multiple spaces/tabs/newlines into single space,
    # and strip leading/trailing space.
    cleaned = re.sub(r'\s+', ' ', cleaned).strip()

    return cleaned, count


def process_file(input_path: Path, output_path: Path) -> None:
    data = json.loads(input_path.read_text(encoding="utf-8"))

    if not isinstance(data, list):
        raise SystemExit(
            f"ERROR: expected top-level JSON array in {input_path}")

    for idx, entry in enumerate(data):
        if not isinstance(entry, dict):
            # skip non-object entries (defensive)
            continue
        cmd = entry.get("command")
        if isinstance(cmd, str):
            cleaned, removals = clean_command_string(cmd)
            if removals > 0:
                entry["command"] = cleaned

    # Ensure parent directory exists
    output_path.parent.mkdir(parents=True, exist_ok=True)
    # Write with indentation for readability
    output_path.write_text(json.dumps(
        data, indent=0, ensure_ascii=False) + "\n", encoding="utf-8")


def main(argv):
    if len(argv) != 3:
        print("Usage: python3 rm-gxx-pch.py input_file output_file")
        return 2
    input_path = Path(argv[1])
    output_path = Path(argv[2])

    if not input_path.exists():
        print(f"ERROR: input file not found: {input_path}")
        return 3

    try:
        process_file(input_path, output_path)
    except json.JSONDecodeError as e:
        print(f"ERROR: failed to parse JSON: {e}")
        return 4
    except Exception as e:
        print(f"ERROR: {e}")
        return 5

    return 0


if __name__ == "__main__":
    main(sys.argv)

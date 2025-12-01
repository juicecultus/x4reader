#!/usr/bin/env python3
"""extract_chars.py

Extract unique characters (including spaces) from a text file or all .txt files in a directory and write them
to an output file (one line, characters concatenated). By default reads
all .txt files in `data/` and writes `scripts/chars_input.txt`.

Usage:
  python scripts/extract_chars.py [input_file] [output_file]

Options:
    By default the output is sorted by Unicode codepoint. Use
    `--preserve-order` to keep the first-seen order instead.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import sys


def extract_unique_chars(text: str, keep_whitespace: bool = True) -> str:
    seen = set()
    ordered = []
    for ch in text:
        if ch not in seen:
            seen.add(ch)
            ordered.append(ch)
    return "".join(ordered)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Extract unique characters from a file"
    )
    parser.add_argument(
        "input",
        nargs="?",
        default="data",
        help="Input text file or directory containing .txt files (default: data)",
    )
    parser.add_argument(
        "output",
        nargs="?",
        default="scripts/chars_input.txt",
        help="Output file to write characters (default: scripts/chars_input.txt)",
    )
    parser.add_argument(
        "--preserve-order",
        action="store_true",
        help="Preserve first-seen order instead of sorting (default: sort)",
    )
    args = parser.parse_args(argv)

    input_path = Path(args.input)
    if input_path.is_dir():
        text = ""
        for file_path in input_path.glob("*.txt"):
            text += file_path.read_text(encoding="utf-8")
    else:
        if not input_path.exists():
            print(f"Error: input file '{input_path}' not found", file=sys.stderr)
            return 2
        text = input_path.read_text(encoding="utf-8")

    output_path = Path(args.output)

    # By default we sort the unique characters. Use --preserve-order to keep
    # the original first-seen ordering.
    if not args.preserve_order:
        chars = sorted(set(text))
        out = "".join(chars)
    else:
        out = extract_unique_chars(text, keep_whitespace=True)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(out + "\n", encoding="utf-8")

    print(f"Wrote {len(out)} characters to '{output_path}'")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

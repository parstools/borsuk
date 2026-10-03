#!/usr/bin/env python3
"""Legacy rule-only draft generator (does not create standalone format 1 documents).

Use `sema --from-ag INPUT.ag --emit-template OUTPUT.sema` for supported templates.
This historical script lacks canonical parsing, stable IDs and provenance.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


@dataclass(frozen=True)
class Token:
    value: str
    start: int
    end: int


def tokenize(source: str) -> list[Token]:
    tokens: list[Token] = []
    position = 0
    while position < len(source):
        char = source[position]
        if char.isspace():
            position += 1
            continue
        if source.startswith("//", position):
            newline = source.find("\n", position + 2)
            position = len(source) if newline < 0 else newline + 1
            continue
        if source.startswith("/*", position):
            end = source.find("*/", position + 2)
            if end < 0:
                raise ValueError("unterminated block comment")
            position = end + 2
            continue

        start = position
        if char in "'\"[":
            closing = "]" if char == "[" else char
            position += 1
            while position < len(source):
                if source[position] == "\\":
                    position += 2
                elif source[position] == closing:
                    position += 1
                    break
                else:
                    position += 1
            else:
                raise ValueError(f"unterminated literal at offset {start}")
            tokens.append(Token(source[start:position], start, position))
            continue

        match = IDENTIFIER.match(source, position)
        if match:
            position += len(match.group())
        else:
            position += 1
        tokens.append(Token(source[start:position], start, position))
    return tokens


def parser_rules(source: str) -> list[str]:
    tokens = tokenize(source)
    rules: list[str] = []
    index = 0
    while index + 2 < len(tokens):
        keyword, name, colon = tokens[index:index + 3]
        line_start = source.rfind("\n", 0, keyword.start) + 1
        if (
            keyword.value not in ("node", "inline")
            or source[line_start:keyword.start].strip()
            or not IDENTIFIER.fullmatch(name.value)
            or colon.value != ":"
        ):
            index += 1
            continue

        separators: list[Token] = []
        depth = 0
        cursor = index + 3
        while cursor < len(tokens):
            token = tokens[cursor]
            if token.value == "(":
                depth += 1
            elif token.value == ")":
                depth -= 1
                if depth < 0:
                    raise ValueError(f"unmatched ')' in rule {name.value}")
            elif depth == 0 and token.value in ("|", ";"):
                separators.append(token)
                if token.value == ";":
                    break
            cursor += 1
        else:
            raise ValueError(f"unterminated parser rule {name.value}")
        if depth:
            raise ValueError(f"unclosed group in rule {name.value}")

        output = source[keyword.start:colon.end]
        previous = colon.end
        for separator in separators:
            alternative = source[previous:separator.start]
            content = alternative.rstrip()
            whitespace = alternative[len(content):]
            output += content + "\n        analysis {}"
            output += whitespace if "\n" in whitespace else "\n    "
            output += separator.value
            previous = separator.end
        rules.append(output)
        index = cursor + 1
    return rules


def main() -> None:
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("source", type=Path, help="input .ag grammar")
    argument_parser.add_argument("-o", "--output", type=Path, help="output .sema.txt path")
    argument_parser.add_argument("--force", action="store_true", help="replace an existing output file")
    args = argument_parser.parse_args()

    if args.source.suffix != ".ag":
        argument_parser.error("source file must have the .ag extension")
    destination = args.output or args.source.with_suffix(".sema.txt")
    try:
        rules = parser_rules(args.source.read_text(encoding="utf-8"))
        if not rules:
            raise ValueError("no parser rules found")
        with destination.open("w" if args.force else "x", encoding="utf-8") as output:
            output.write("\n\n".join(rules) + "\n")
    except (OSError, ValueError) as error:
        argument_parser.error(str(error))
    print(f"{destination}: {len(rules)} parser rules")


if __name__ == "__main__":
    main()

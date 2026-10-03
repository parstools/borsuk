#!/usr/bin/env python3
"""Mechanical ANTLR4 combined-grammar to restricted Agas converter.

This converter intentionally handles the action-free grammar subset used by
the legacy C90 files in ``../grammars/c``. Parser-side groups are extracted to
named inline rules. Lexer-side regular expressions retain their groups.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import pathlib
import re
from collections import Counter, defaultdict


@dataclasses.dataclass
class Atom:
    text: str
    suffix: str = ""


@dataclasses.dataclass
class Group:
    alternatives: list["Alternative"]
    suffix: str = ""


@dataclasses.dataclass
class Alternative:
    terms: list[Atom | Group]
    label: str | None = None


@dataclasses.dataclass
class Rule:
    name: str
    body: list[str]
    fragment: bool = False

    @property
    def parser_rule(self) -> bool:
        return self.name[0].islower()


def tokenize(text: str) -> list[str]:
    tokens: list[str] = []
    i = 0
    while i < len(text):
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if text.startswith("//", i):
            end = text.find("\n", i + 2)
            i = len(text) if end < 0 else end + 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise ValueError("unterminated block comment")
            i = end + 2
            continue
        if c == "'":
            start = i
            i += 1
            escaped = False
            while i < len(text):
                current = text[i]
                i += 1
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == "'":
                    break
            else:
                raise ValueError("unterminated string literal")
            tokens.append(text[start:i])
            continue
        if c == "[":
            start = i
            i += 1
            escaped = False
            while i < len(text):
                current = text[i]
                i += 1
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == "]":
                    break
            else:
                raise ValueError("unterminated character set")
            tokens.append(text[start:i])
            continue
        if c.isalpha() or c == "_":
            match = re.match(r"[A-Za-z_][A-Za-z_0-9]*", text[i:])
            assert match is not None
            token = match.group(0)
            tokens.append(token)
            i += len(token)
            continue
        if text.startswith("->", i):
            tokens.append("->")
            i += 2
            continue
        if text.startswith("+=", i):
            tokens.append("+=")
            i += 2
            continue
        tokens.append(c)
        i += 1
    return tokens


def extract_rules(tokens: list[str]) -> tuple[str, list[Rule]]:
    try:
        grammar_index = tokens.index("grammar")
    except ValueError as error:
        raise ValueError("missing grammar declaration") from error
    grammar_name = tokens[grammar_index + 1]
    index = grammar_index + 2
    if tokens[index] != ";":
        raise ValueError("invalid grammar declaration")
    index += 1
    rules: list[Rule] = []
    while index < len(tokens):
        fragment = False
        if tokens[index] == "fragment":
            fragment = True
            index += 1
        if index >= len(tokens):
            break
        name = tokens[index]
        index += 1
        if index >= len(tokens) or tokens[index] != ":":
            raise ValueError(f"expected ':' after top-level name {name!r}")
        index += 1
        body: list[str] = []
        while index < len(tokens) and tokens[index] != ";":
            body.append(tokens[index])
            index += 1
        if index >= len(tokens):
            raise ValueError(f"unterminated rule {name!r}")
        index += 1
        rules.append(Rule(name=name, body=body, fragment=fragment))
    return grammar_name, rules


class ParserBodyParser:
    def __init__(self, tokens: list[str]):
        self.tokens = tokens
        self.index = 0

    def peek(self) -> str | None:
        if self.index == len(self.tokens):
            return None
        return self.tokens[self.index]

    def take(self) -> str:
        token = self.peek()
        if token is None:
            raise ValueError("unexpected end of parser rule")
        self.index += 1
        return token

    def alternatives(self, stop: str | None = None) -> list[Alternative]:
        result: list[Alternative] = []
        while True:
            terms: list[Atom | Group] = []
            while self.peek() not in {None, "|", stop, "#"}:
                terms.append(self.term())
            label = None
            if self.peek() == "#":
                self.take()
                label = self.take()
            result.append(Alternative(terms=terms, label=label))
            if self.peek() != "|":
                break
            self.take()
        return result

    def term(self) -> Atom | Group:
        if self.peek() == "(":
            self.take()
            value: Atom | Group = Group(self.alternatives(stop=")"))
            if self.take() != ")":
                raise ValueError("unclosed parser group")
        else:
            value = Atom(self.take())
        if self.peek() in {"?", "*", "+"}:
            value.suffix = self.take()
        return value


def capitalized_identifier(name: str) -> str:
    parts = [part for part in re.split(r"[^A-Za-z0-9]+", name) if part]
    if not parts:
        return "Alternative"
    value = "".join(part[0].upper() + part[1:] for part in parts)
    if not value[0].isupper():
        value = "Alternative" + value
    return value


class ParserRuleConverter:
    def __init__(self, existing_names: set[str]):
        self.existing_names = set(existing_names)
        self.group_counters: defaultdict[str, int] = defaultdict(int)

    def next_group_name(self, owner: str) -> str:
        while True:
            self.group_counters[owner] += 1
            candidate = f"{owner}Group{self.group_counters[owner]}"
            if candidate not in self.existing_names:
                self.existing_names.add(candidate)
                return candidate

    def lower_groups(
        self, owner: str, alternatives: list[Alternative]
    ) -> tuple[list[Alternative], list[tuple[str, list[Alternative]]]]:
        helpers: list[tuple[str, list[Alternative]]] = []
        lowered: list[Alternative] = []
        for alternative in alternatives:
            terms: list[Atom] = []
            for term in alternative.terms:
                if isinstance(term, Atom):
                    terms.append(term)
                    continue
                helper_name = self.next_group_name(owner)
                helper_alts, nested = self.lower_groups(owner, term.alternatives)
                helpers.extend(nested)
                helpers.append((helper_name, helper_alts))
                terms.append(Atom(helper_name, term.suffix))
            lowered.append(Alternative(terms=terms, label=alternative.label))
        return lowered, helpers


def normalize_labels(rule_name: str, alternatives: list[Alternative]) -> None:
    if not any(alternative.label for alternative in alternatives):
        return
    raw = [
        capitalized_identifier(alternative.label)
        if alternative.label
        else f"{capitalized_identifier(rule_name)}Alternative{index}"
        for index, alternative in enumerate(alternatives, 1)
    ]
    totals = Counter(raw)
    seen: Counter[str] = Counter()
    for alternative, label in zip(alternatives, raw):
        seen[label] += 1
        alternative.label = (
            f"{label}{seen[label]}" if totals[label] > 1 else label
        )


def format_parser_rule(
    name: str, alternatives: list[Alternative], modifier: str
) -> str:
    normalize_labels(name, alternatives)
    lines = [f"{modifier} {name}"]
    for index, alternative in enumerate(alternatives):
        prefix = "    : " if index == 0 else "    | "
        body = " ".join(term.text + term.suffix for term in alternative.terms)
        if not body:
            body = "empty"
        if alternative.label:
            body += f" #{alternative.label}"
        lines.append(prefix + body)
    lines.append("    ;")
    return "\n".join(lines)


def format_lexer_rule(rule: Rule) -> str:
    prefix = "fragment " if rule.fragment else ""
    body = " ".join(rule.body)
    return f"{prefix}{rule.name}\n    : {body}\n    ;"


def leading_license(text: str) -> str:
    stripped = text.lstrip()
    if not stripped.startswith("/*"):
        return ""
    end = stripped.find("*/")
    if end < 0:
        return ""
    return stripped[: end + 2]


def convert(source: pathlib.Path, grammar_name: str) -> str:
    text = source.read_text(encoding="utf-8")
    tokens = tokenize(text)
    _, rules = extract_rules(tokens)
    existing = {rule.name for rule in rules}
    converter = ParserRuleConverter(existing)
    output: list[str] = []
    license_text = leading_license(text)
    if license_text:
        output.append(license_text)
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    output.append(
        "/*\n"
        " * Mechanical conversion of a legacy ANTLR4 grammar to restricted Agas.\n"
        f" * Source: {source}\n"
        f" * Source SHA-256: {digest}\n"
        " * Anonymous parser groups were extracted into named inline rules.\n"
        " */"
    )
    output.append(f"grammar {grammar_name};")
    output.append(
        "options {\n"
        "    parser = LR;\n"
        "    lookahead = 1;\n"
        "    ast = concrete;\n"
        "    legacy = true;\n"
        "}"
    )
    output.append("channels {\n    HIDDEN\n}")
    for rule in rules:
        if not rule.parser_rule:
            output.append(format_lexer_rule(rule))
            continue
        parser = ParserBodyParser(rule.body)
        alternatives = parser.alternatives()
        if parser.peek() is not None:
            raise ValueError(
                f"unparsed token {parser.peek()!r} in parser rule {rule.name}"
            )
        alternatives, helpers = converter.lower_groups(rule.name, alternatives)
        output.append(format_parser_rule(rule.name, alternatives, "node"))
        for helper_name, helper_alternatives in helpers:
            output.append(
                format_parser_rule(helper_name, helper_alternatives, "inline")
            )
    return "\n\n".join(output) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("destination", type=pathlib.Path)
    parser.add_argument("--grammar-name", required=True)
    args = parser.parse_args()
    result = convert(args.source, args.grammar_name)
    args.destination.write_text(result, encoding="utf-8")


if __name__ == "__main__":
    main()

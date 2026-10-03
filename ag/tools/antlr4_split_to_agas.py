#!/usr/bin/env python3
"""Convert a split ANTLR4 parser/lexer grammar to restricted Agas.

This is a mechanical importer for grammar snapshots used as reference inputs.
Target-language actions and semantic predicates are deliberately removed and
reported in the generated file; their semantics must be implemented separately.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re

from antlr4_to_agas import (
    ParserBodyParser,
    ParserRuleConverter,
    Rule,
    format_lexer_rule,
    format_parser_rule,
    leading_license,
    tokenize,
)


def find_balanced_brace_end(text: str, start: int) -> int:
    if text[start] != "{":
        raise ValueError("expected opening brace")
    depth = 1
    index = start + 1
    quote: str | None = None
    escaped = False
    while index < len(text):
        char = text[index]
        if quote is not None:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            index += 1
            continue
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            index = len(text) if end < 0 else end + 1
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                raise ValueError("unterminated comment in braced block")
            index = end + 2
            continue
        if char in {'"', "'"}:
            quote = char
            index += 1
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise ValueError("unterminated braced block")


def remove_named_block(text: str, name: str) -> tuple[str, str | None]:
    match = re.search(rf"\b{re.escape(name)}\s*\{{", text)
    if match is None:
        return text, None
    brace = text.find("{", match.start())
    end = find_balanced_brace_end(text, brace)
    block = text[brace + 1 : end - 1]
    return text[: match.start()] + "\n" + text[end:], block


def grammar_body(text: str, kind: str) -> str:
    declaration = re.search(
        rf"\b{re.escape(kind)}\s+grammar\s+[A-Za-z_][A-Za-z_0-9]*\s*;",
        text,
    )
    if declaration is None:
        raise ValueError(f"missing {kind} grammar declaration")
    return text[declaration.end() :]


def strip_parser_actions(text: str) -> tuple[str, int, int]:
    output: list[str] = []
    actions = 0
    predicates = 0
    index = 0
    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = len(text) if end < 0 else end + 1
            output.append(text[index:end])
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end < 0:
                raise ValueError("unterminated block comment")
            end += 2
            output.append(text[index:end])
            index = end
            continue
        if text[index] == "'":
            start = index
            index += 1
            escaped = False
            while index < len(text):
                char = text[index]
                index += 1
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == "'":
                    break
            else:
                raise ValueError("unterminated ANTLR literal")
            output.append(text[start:index])
            continue
        if text[index] != "{":
            output.append(text[index])
            index += 1
            continue

        index = find_balanced_brace_end(text, index)
        lookahead = index
        while lookahead < len(text) and text[lookahead] in " \t":
            lookahead += 1
        if lookahead < len(text) and text[lookahead] == "?":
            predicates += 1
            index = lookahead + 1
        else:
            actions += 1
        output.append(" ")
    return "".join(output), actions, predicates


def extract_rule_list(text: str) -> list[Rule]:
    tokens = tokenize(text)
    rules: list[Rule] = []
    index = 0
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
    return rules


def channel_names(block: str | None) -> list[str]:
    if block is None:
        return []
    return re.findall(r"[A-Za-z_][A-Za-z_0-9]*", block)


def sha256(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def lower_negated_token_sets(
    parser_rules: list[Rule], lexer_rules: list[Rule]
) -> tuple[list[Rule], set[str]]:
    """Replace parser-side ~(A|B) with an explicit inline token rule."""
    vocabulary = [rule for rule in lexer_rules if not rule.fragment]
    existing = {rule.name for rule in parser_rules + lexer_rules}
    helpers: list[Rule] = []
    helper_names: set[str] = set()

    for rule in parser_rules:
        rewritten: list[str] = []
        index = 0
        counter = 0
        while index < len(rule.body):
            if rule.body[index] != "~":
                rewritten.append(rule.body[index])
                index += 1
                continue

            index += 1
            excluded: set[str] = set()
            if index < len(rule.body) and rule.body[index] == "(":
                index += 1
                while index < len(rule.body) and rule.body[index] != ")":
                    token = rule.body[index]
                    if token != "|":
                        excluded.add(token)
                    index += 1
                if index >= len(rule.body):
                    raise ValueError(f"unclosed negated set in {rule.name}")
                index += 1
            elif index < len(rule.body):
                excluded.add(rule.body[index])
                index += 1
            else:
                raise ValueError(f"missing operand after '~' in {rule.name}")

            counter += 1
            helper_name = f"{rule.name}NotSet{counter}"
            while helper_name in existing:
                counter += 1
                helper_name = f"{rule.name}NotSet{counter}"
            existing.add(helper_name)
            helper_names.add(helper_name)

            allowed: list[str] = []
            for lexer_rule in vocabulary:
                if lexer_rule.name in excluded:
                    continue
                if len(lexer_rule.body) == 1 and lexer_rule.body[0] in excluded:
                    continue
                allowed.append(lexer_rule.name)
            if not allowed:
                raise ValueError(f"empty complement of token set in {rule.name}")
            helper_body: list[str] = []
            for allowed_name in allowed:
                if helper_body:
                    helper_body.append("|")
                helper_body.append(allowed_name)
            helpers.append(Rule(name=helper_name, body=helper_body))
            rewritten.append(helper_name)
        rule.body = rewritten

    return [*parser_rules, *helpers], helper_names


def convert(
    parser_source: pathlib.Path,
    lexer_source: pathlib.Path,
    grammar_name: str,
) -> str:
    parser_text = parser_source.read_text(encoding="utf-8")
    lexer_text = lexer_source.read_text(encoding="utf-8")

    parser_body = grammar_body(parser_text, "parser")
    parser_body, _ = remove_named_block(parser_body, "options")
    parser_body, actions, predicates = strip_parser_actions(parser_body)

    lexer_body = grammar_body(lexer_text, "lexer")
    lexer_body, _ = remove_named_block(lexer_body, "options")
    lexer_body, channels_block = remove_named_block(lexer_body, "channels")

    parser_rules = extract_rule_list(parser_body)
    lexer_rules = extract_rule_list(lexer_body)
    parser_rules, negated_set_helpers = lower_negated_token_sets(
        parser_rules, lexer_rules
    )
    existing_names = {rule.name for rule in parser_rules + lexer_rules}
    converter = ParserRuleConverter(existing_names)

    output: list[str] = []
    license_text = leading_license(parser_text)
    if license_text:
        output.append(license_text)
    output.append(
        "/*\n"
        " * Mechaniczna konwersja rozdzielonej gramatyki ANTLR4 do ograniczonego Agas.\n"
        f" * Parser: {parser_source}\n"
        f" * SHA-256 parsera: {sha256(parser_text)}\n"
        f" * Lekser: {lexer_source}\n"
        f" * SHA-256 leksera: {sha256(lexer_text)}\n"
        f" * Usunięte akcje języka docelowego: {actions}.\n"
        f" * Usunięte predykaty semantyczne: {predicates}.\n"
        " * Ich zachowanie, zwłaszcza rozpoznawanie typedef i zakresów nazw,\n"
        " * trzeba odtworzyć poza bezkontekstową gramatyką Agas.\n"
        " * Anonimowe grupy parsera zostały wydzielone do reguł inline.\n"
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
    channels = ["HIDDEN", *channel_names(channels_block)]
    channels = list(dict.fromkeys(channels))
    output.append("channels {\n    " + ",\n    ".join(channels) + "\n}")

    for rule in parser_rules:
        parser = ParserBodyParser(rule.body)
        alternatives = parser.alternatives()
        if parser.peek() is not None:
            raise ValueError(
                f"unparsed token {parser.peek()!r} in parser rule {rule.name}"
            )
        alternatives, helpers = converter.lower_groups(rule.name, alternatives)
        modifier = "inline" if rule.name in negated_set_helpers else "node"
        output.append(format_parser_rule(rule.name, alternatives, modifier))
        for helper_name, helper_alternatives in helpers:
            output.append(
                format_parser_rule(helper_name, helper_alternatives, "inline")
            )

    output.extend(format_lexer_rule(rule) for rule in lexer_rules)
    return "\n\n".join(output) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("parser_source", type=pathlib.Path)
    parser.add_argument("lexer_source", type=pathlib.Path)
    parser.add_argument("destination", type=pathlib.Path)
    parser.add_argument("--grammar-name", required=True)
    args = parser.parse_args()
    result = convert(
        args.parser_source,
        args.lexer_source,
        args.grammar_name,
    )
    args.destination.write_text(result, encoding="utf-8")


if __name__ == "__main__":
    main()

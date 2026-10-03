#!/usr/bin/env python3
"""Compare parser productions in an Agas grammar and an AgSem file."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from dataclasses import dataclass
from pathlib import Path

from ag_to_sema_template import Token, tokenize


@dataclass(frozen=True)
class Alternative:
    signature: tuple[str, ...]
    start: int
    end: int
    line: int


@dataclass(frozen=True)
class Rule:
    kind: str
    name: str
    start: int
    end: int
    colon_end: int
    separators: tuple[Token, ...]
    alternatives: tuple[Alternative, ...]
    line: int


def line_number(source: str, offset: int) -> int:
    return source.count("\n", 0, offset) + 1


def extract_rules(source: str, semantic: bool) -> list[Rule]:
    tokens = tokenize(source)
    rules: list[Rule] = []
    index = 0
    while index + 2 < len(tokens):
        kind, name, colon = tokens[index:index + 3]
        line_start = source.rfind("\n", 0, kind.start) + 1
        if (kind.value not in ("node", "inline")
                or source[line_start:kind.start].strip()
                or colon.value != ":"):
            index += 1
            continue

        separators: list[Token] = []
        alternatives: list[Alternative] = []
        grammar_tokens: list[str] = []
        cursor = index + 3
        start = colon.end
        in_actions = False
        while cursor < len(tokens):
            token = tokens[cursor]
            if token.value in ("|", ";"):
                alternatives.append(Alternative(tuple(grammar_tokens), start, token.start,
                                                line_number(source, start)))
                separators.append(token)
                if token.value == ";":
                    break
                start = token.end
                grammar_tokens = []
                in_actions = False
                cursor += 1
                continue
            if semantic and (token.value == "analysis" or
                             token.value == "execution" and cursor + 1 < len(tokens)
                             and tokens[cursor + 1].value == "result"):
                in_actions = True
                cursor += 1 if token.value == "analysis" else 2
                if cursor >= len(tokens) or tokens[cursor].value != "{":
                    raise ValueError(f"expected action block in rule {name.value}")
                depth = 1
                cursor += 1
                while cursor < len(tokens) and depth:
                    if tokens[cursor].value == "{":
                        depth += 1
                    elif tokens[cursor].value == "}":
                        depth -= 1
                    cursor += 1
                if depth:
                    raise ValueError(f"unclosed action block in rule {name.value}")
                continue
            if in_actions:
                raise ValueError(f"grammar element after action in rule {name.value}")
            grammar_tokens.append(token.value)
            cursor += 1
        else:
            raise ValueError(f"unterminated parser rule {name.value}")

        rules.append(Rule(kind.value, name.value, kind.start, separators[-1].end,
                          colon.end, tuple(separators), tuple(alternatives),
                          line_number(source, kind.start)))
        index = cursor + 1
    if not rules:
        raise ValueError("no parser rules found")
    names = [rule.name for rule in rules]
    repeated = [name for name, count in Counter(names).items() if count > 1]
    if repeated:
        raise ValueError("duplicate parser rules: " + ", ".join(repeated))
    return rules


def signature_text(signature: tuple[str, ...]) -> str:
    return " ".join(signature) or "<empty>"


def compare(source_rules: list[Rule], sema_rules: list[Rule]) -> tuple[list[str], bool]:
    messages: list[str] = []
    source_by_name = {rule.name: rule for rule in source_rules}
    sema_by_name = {rule.name: rule for rule in sema_rules}
    missing = sorted(source_by_name.keys() - sema_by_name.keys())
    extra = sorted(sema_by_name.keys() - source_by_name.keys())
    if missing:
        messages.append("ERROR missing rules: " + ", ".join(missing))
    if extra:
        messages.append("ERROR extra rules: " + ", ".join(extra))

    compatible = not (missing or extra)
    for source_rule in source_rules:
        sema_rule = sema_by_name.get(source_rule.name)
        if sema_rule is None:
            continue
        if source_rule.kind != sema_rule.kind:
            messages.append(f"ERROR {source_rule.name}: kind {sema_rule.kind} "
                            f"(sema:{sema_rule.line}) != {source_rule.kind} "
                            f"(ag:{source_rule.line})")
            compatible = False
        source_sigs = [alt.signature for alt in source_rule.alternatives]
        sema_sigs = [alt.signature for alt in sema_rule.alternatives]
        missing_sigs = Counter(source_sigs) - Counter(sema_sigs)
        extra_sigs = Counter(sema_sigs) - Counter(source_sigs)
        for sig, count in missing_sigs.items():
            messages.append(f"ERROR {source_rule.name}: missing alternative "
                            f"{signature_text(sig)} (count {count}, ag:{source_rule.line})")
            compatible = False
        for sig, count in extra_sigs.items():
            messages.append(f"ERROR {source_rule.name}: extra alternative "
                            f"{signature_text(sig)} (count {count}, sema:{sema_rule.line})")
            compatible = False
        if Counter(source_sigs) == Counter(sema_sigs) and source_sigs != sema_sigs:
            locations = defaultdict(deque)
            for number, sig in enumerate(sema_sigs, 1):
                locations[sig].append(number)
            permutation = [locations[sig].popleft() for sig in source_sigs]
            messages.append(f"WARNING {source_rule.name}: alternative order "
                            f"sema:{sema_rule.line} differs from ag:{source_rule.line}; "
                            f"move alternatives to positions {permutation}")

    if not missing and not extra:
        source_names = [rule.name for rule in source_rules]
        sema_names = [rule.name for rule in sema_rules]
        if source_names != sema_names:
            old_positions = {name: index for index, name in enumerate(sema_names, 1)}
            moves = [f"{name} {old_positions[name]}->{index}"
                     for index, name in enumerate(source_names, 1)
                     if old_positions[name] != index]
            messages.append("WARNING rule order differs (sema->ag positions): "
                            + ", ".join(moves))
    return messages, compatible


def reorder_alternatives(source_rule: Rule, sema_rule: Rule, sema_text: str) -> str:
    locations = defaultdict(deque)
    for alt in sema_rule.alternatives:
        locations[alt.signature].append(sema_text[alt.start:alt.end])
    chunks = [locations[alt.signature].popleft()
              for alt in source_rule.alternatives]
    text = sema_text[sema_rule.start:sema_rule.colon_end]
    for chunk, separator in zip(chunks, sema_rule.separators):
        text += chunk + separator.value
    return text


def reordered_text(source_rules: list[Rule], sema_rules: list[Rule], sema_text: str) -> str:
    sema_by_name = {rule.name: rule for rule in sema_rules}
    gaps = [sema_text[sema_rules[index].end:sema_rules[index + 1].start]
            for index in range(len(sema_rules) - 1)]
    if any(gap.strip() for gap in gaps):
        raise ValueError("text between parser rules prevents lossless reordering")
    output = sema_text[:sema_rules[0].start]
    for index, source_rule in enumerate(source_rules):
        output += reorder_alternatives(source_rule, sema_by_name[source_rule.name],
                                       sema_text)
        if index < len(gaps):
            output += gaps[index]
    output += sema_text[sema_rules[-1].end:]
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("grammar", type=Path, help="source .ag grammar")
    parser.add_argument("sema", type=Path, help="semantic .sema file")
    parser.add_argument("-o", "--output", type=Path,
                        help="write reordered copy (only if structures match)")
    args = parser.parse_args()
    try:
        grammar = args.grammar.read_bytes().decode("utf-8")
        sema = args.sema.read_bytes().decode("utf-8")
        source_rules = extract_rules(grammar, semantic=False)
        sema_rules = extract_rules(sema, semantic=True)
        messages, compatible = compare(source_rules, sema_rules)
        for message in messages:
            print(message)
        if not compatible:
            parser.exit(1, "Comparison failed; reordered copy was not written.\n")
        print(f"OK: {len(source_rules)} rules and "
              f"{sum(len(rule.alternatives) for rule in source_rules)} alternatives match")
        if args.output:
            if args.output.resolve() in (args.grammar.resolve(), args.sema.resolve()):
                raise ValueError("output must differ from both input files")
            output = reordered_text(source_rules, sema_rules, sema)
            with args.output.open("xb") as stream:
                stream.write(output.encode("utf-8"))
            print(f"Wrote reordered copy: {args.output}")
    except (OSError, ValueError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()

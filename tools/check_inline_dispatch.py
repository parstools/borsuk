#!/usr/bin/env python3
"""Check Rust dispatch for an unlabeled inline rule forwarding AST children."""

import argparse
from pathlib import Path
import re
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--coge", "--sema", dest="sema", help="coge binary (legacy adapter; --sema is a compatibility alias)", required=True)
    parser.add_argument("--agas", required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    grammar = (args.source_dir / "scope_codegen.ag").read_text()
    semantics = (args.source_dir / "scope_codegen.sema").read_text()
    semantics = semantics.replace(
        "semantic_model {",
        "semantic_model {\n"
        "    analyzer item(flow: FlowId, scope: ScopeId) -> FlowId;\n"
        "    analyzer assignment(flow: FlowId, scope: ScopeId) -> FlowId;\n"
        "    analyzer wrapper(flow: FlowId, scope: ScopeId) -> FlowId;\n"
        "    analyzer pair(flow: FlowId, scope: ScopeId) -> FlowId;\n"
        "    analyzer sign(flow: FlowId, scope: ScopeId) -> FlowId;\n"
        "    analyzer signedPair(flow: FlowId, scope: ScopeId) -> FlowId;",
        1,
    )
    grammar = grammar.replace("items=statement*", "items=item*")
    semantics = semantics.replace("items=statement*", "items=item*")
    grammar += "\ninline item\n    : value=statement\n    | SEMI value=block SEMI\n    ;\n"
    grammar += "\nnode assignment\n    : target=ID ASSIGN value=NUMBER SEMI\n    ;\n"
    grammar += "\nnode wrapper\n    : value=assignment #ForwardedAssignment\n    ;\n"
    grammar += "\ninline pair\n    : left=assignment right=assignment\n    ;\n"
    grammar += "\ninline sign\n    : value=PLUS\n    | value=MINUS\n    ;\n"
    grammar += "\ninline signedPair\n    : operator=sign operand=assignment\n    ;\n"
    grammar += "\nPLUS : '+';\nMINUS : '-';\n"
    semantics = semantics.replace(
        'for "scope_codegen.ag"', 'for "inline_dispatch.ag"'
    )
    semantics += (
        "\ninline item\n"
        "    : value=statement analysis {\n"
        "        analyze value with flow: flow, scope: scope -> analyzed;\n"
        "        result = analyzed;\n"
        "      }\n"
        "    | SEMI value=block SEMI analysis {\n"
        "        analyze value with flow: flow, scope: scope -> analyzed;\n"
        "        result = analyzed;\n"
        "      }\n"
        "    ;\n"
        "\nnode assignment\n"
        "    : target=ID ASSIGN value=NUMBER SEMI analysis {\n"
        "        result = flow;\n"
        "      }\n"
        "    ;\n"
        "\nnode wrapper\n"
        "    : value=assignment #ForwardedAssignment analysis {\n"
        "        analyze value with flow: flow, scope: scope -> analyzed;\n"
        "        result = analyzed;\n"
        "      }\n"
        "    ;\n"
        "\ninline pair\n"
        "    : left=assignment right=assignment analysis {\n"
        "        analyze left with flow: flow, scope: scope -> before;\n"
        "        analyze right with flow: before, scope: scope -> after;\n"
        "        result = after;\n"
        "      }\n"
        "    ;\n"
        "\ninline sign\n"
        "    : value=PLUS analysis { result = flow; }\n"
        "    | value=MINUS analysis { result = flow; }\n"
        "    ;\n"
        "\ninline signedPair\n"
        "    : operator=sign operand=assignment analysis {\n"
        "        analyze operator with flow: flow, scope: scope -> selected;\n"
        "        analyze operand with flow: selected, scope: scope -> after;\n"
        "        result = after;\n"
        "      }\n"
        "    ;\n"
    )
    (args.output_dir / "inline_dispatch.ag").write_text(grammar)
    source = args.output_dir / "inline_dispatch.sema"
    source.write_text(semantics)
    subprocess.run(
        [args.sema, "--legacy", "--emit-rust-dir", str(args.output_dir), str(source)],
        check=True,
    )
    parser_output = args.output_dir / "parser_gen.rs"
    subprocess.run(
        [args.agas, "--emit-rust-parser", str(parser_output),
         str(args.output_dir / "inline_dispatch.ag")],
        check=True,
        capture_output=True,
        text=True,
    )
    generated = (args.output_dir / "sema_gen.rs").read_text()
    parser_generated = parser_output.read_text()
    for fragment in (
        'if node.type_name == "statement" || (node.kind != AstValueKind::Node && is_statement_ast(node)) {\n        return analyze_item_alt_0',
        'if node.type_name == "block" || (node.kind != AstValueKind::Node && is_block_ast(node)) {\n        return analyze_item_alt_1',
        "let value = node;",
        "fn is_item_node(value: &AstValue) -> bool",
        'pub fn analyze_assignment(',
        '"" => analyze_assignment_alt_0(ctx, flow, scope, node)',
        'if node.type_name == "assignment" {\n        return analyze_wrapper_alt_0',
        "fn analyze_wrapper_alt_0(",
        "fn is_pair_ast(value: &AstValue) -> bool",
        'value.kind == AstValueKind::Record && value.type_name == "pair"',
        "fn is_sign_ast(value: &AstValue) -> bool",
        "match node.token_kind {",
        "fn is_signed_pair_ast(value: &AstValue) -> bool",
    ):
        if fragment not in generated:
            raise AssertionError(f"missing generated Rust fragment: {fragment}")
    wrapper = generated.split("fn analyze_wrapper_alt_0(", maxsplit=1)[1]
    for fragment in (
        "let value = node;",
        "let analyzed = analyze_assignment(&mut *ctx, flow, scope, value)?;",
        "let result = analyzed;",
    ):
        if fragment not in wrapper:
            raise AssertionError(f"missing wrapper analysis action: {fragment}")
    pair = generated.split("fn analyze_pair_alt_0(", maxsplit=1)[1]
    for fragment in (
        'let left = child("left")?;',
        'let right = child("right")?;',
        "analyze_assignment(&mut *ctx, flow, scope, left)?",
        "analyze_assignment(&mut *ctx, before, scope, right)?",
    ):
        if fragment not in pair:
            raise AssertionError(f"missing pair analysis action: {fragment}")
    sign = generated.split("pub fn analyze_sign(", maxsplit=1)[1]
    if not re.search(r"match node\.token_kind \{\s*\d+ => analyze_sign_alt_0", sign):
        raise AssertionError("missing PLUS token dispatch")
    if not re.search(r"\d+ => analyze_sign_alt_1", sign):
        raise AssertionError("missing MINUS token dispatch")
    if "let value = node;" not in sign:
        raise AssertionError("missing forwarded token binding")
    for name, index in (("PLUS", 0), ("MINUS", 1)):
        token_match = re.search(
            rf'LexerRule \{{ name: "{name}", terminal: Some\((\d+)\)',
            parser_generated,
        )
        if not token_match:
            raise AssertionError(f"missing parser token {name}")
        dispatch = f"{token_match.group(1)} => analyze_sign_alt_{index}"
        if dispatch not in sign:
            raise AssertionError(f"wrong token kind for {name}")
    signed_pair = generated.split("fn analyze_signed_pair_alt_0(", maxsplit=1)[1]
    if "if !is_sign_ast(operator)" not in signed_pair:
        raise AssertionError("record field did not accept forwarded token")


if __name__ == "__main__":
    main()

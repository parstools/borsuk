#!/usr/bin/env python3
"""Verify constructor selection policy validation and generated adapter."""

from coge_cli import MigratedCogeParser, explicit_model_bindings
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    arguments = MigratedCogeParser("toycp")
    arguments.add_argument("--coge", "--sema", dest="coge", help="coge binary (--sema is a compatibility alias)", type=Path, required=True)
    arguments.add_argument("--source", type=Path, required=True)
    arguments.add_argument("--contracts", type=Path, required=True)
    args = arguments.parse_args()
    source = explicit_model_bindings(args.source)
    start = source.index("    selection_policy constructor_choice {")
    end = source.index("    rust_context Context;", start)
    policy = source[start:end]
    source = source[:start] + source[end:]

    with tempfile.TemporaryDirectory(prefix="agsem-selection-policy-") as temporary:
        root = Path(temporary)

        def generate(name: str, declaration: str, expected: str | None = None) -> bytes:
            path = root / f"{name}.coge"
            path.write_text(source.replace(
                "semantic_model {\n", "semantic_model {\n" + declaration, 1
            ))
            output = root / name
            completed = subprocess.run(
                [str(args.coge.resolve()), "--contracts", str(args.contracts.resolve()), "--emit-rust-dir", str(output), str(path)],
                capture_output=True,
                text=True,
                check=False,
            )
            if expected:
                assert completed.returncode != 0 and expected in completed.stderr, (
                    f"{name}: expected {expected!r}\n{completed.stdout}{completed.stderr}"
                )
                return b""
            assert completed.returncode == 0, (
                f"{name}: {completed.stdout}{completed.stderr}"
            )
            return (output / "sema_lib_gen.rs").read_bytes()

        generated = generate("valid", policy)
        assert b"pub fn choose_constructor(" in generated
        assert b"pub fn collect_constructor_candidates(" in generated
        assert b"pub fn complete_construction(" in generated
        assert b"ctx.structs[owner.0].constructors.iter().copied()" in generated
        assert b"Context::conversion_allowed(actual, ty)" in generated
        assert b"ctx.convert(argument, ty.clone())" in generated
        assert b"Operation::Construct(symbol, selected.map(|candidate| candidate.id), converted)" in generated
        assert b"ctx.mark_initialized(&mut flow, place)" in generated
        assert b"let allow_implicit_default = !has_declared && !has_arguments;" in generated
        assert b"select_ranked_candidate(candidates.iter().cloned()" in generated
        assert b"SelectionFailure::Inaccessible" in generated
        assert b'"constructor is not accessible"' in generated

        generate("unknown_field", policy.replace("ranking =", "rank ="),
                 "selection_policy unknown field rank")
        generate("missing_field", policy.replace("        access = after_ranking;\n", ""),
                 "selection_policy missing field access")
        generate("duplicate_field", policy.replace(
            "        access = after_ranking;",
            "        access = after_ranking;\n        access = after_ranking;"),
            "selection_policy duplicate field access")
        generate("wrong_ranking", policy.replace("fewest_conversions", "first_candidate"),
                 "selection_policy unsupported ranking")
        generate("bad_message", policy.replace('missing = "constructor not declared"',
                                                "missing = constructor_not_declared"),
                 "selection_policy missing needs a plain string literal")
        generate("name_collision", policy.replace("choose = choose_constructor",
                                                   "choose = binary_expr"),
                 "duplicate or conflicting declaration:")
        generate("missing_bindings", policy[:policy.index("    selection_bindings")],
                 "sema.missing_binding")
        generate("unknown_binding", policy.replace("expression_type = ty;",
                                                    "type_field = ty;"),
                 "sema.unknown_binding_slot")
        generate("missing_binding", policy.replace("        visibility = visibility;\n", ""),
                 "selection.constructor_choice.visibility: missing explicit binding")
        generate("wrong_receiver", policy.replace("receiver = first_parameter",
                                                   "receiver = no_receiver"),
                 "selection_policy unsupported receiver")
        generate("missing_completion", policy.replace("        complete = complete_construction;\n", ""),
                 "selection_policy missing field complete")
        generate("bad_completion_message", policy.replace(
            'incompatible = "incompatible argument type"',
            'incompatible = incompatible_argument_type'),
            "selection_policy incompatible needs a plain string literal")


if __name__ == "__main__":
    main()

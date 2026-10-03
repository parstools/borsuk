#!/usr/bin/env python3
"""Verify assignment-policy validation and generated Rust adapters."""

from coge_cli import MigratedCogeParser, explicit_model_bindings
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    arguments = MigratedCogeParser()
    arguments.add_argument("--coge", "--sema", dest="coge", help="coge binary (--sema is a compatibility alias)", type=Path, required=True)
    arguments.add_argument("--source", type=Path, required=True)
    arguments.add_argument("--contracts", type=Path, required=True)
    args = arguments.parse_args()
    source = explicit_model_bindings(args.source)
    start = source.index("    assignment_policy store_value {")
    middle = source.index("    assignment_bindings store_value {", start)
    end = source.index("    condition_policy condition_bool {", middle)
    policy = source[start:middle]
    bindings = source[middle:end]
    source = source[:start] + source[end:]

    with tempfile.TemporaryDirectory(prefix="agsem-assignment-policy-") as temporary:
        root = Path(temporary)

        def generate(name: str, policy_text: str, bindings_text: str = bindings,
                     expected: str | None = None) -> dict[str, bytes]:
            path = root / f"{name}.coge"
            path.write_text(source.replace(
                "semantic_model {\n",
                "semantic_model {\n" + policy_text + bindings_text,
                1,
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
                return {}
            assert completed.returncode == 0, (
                f"{name}: {completed.stdout}{completed.stderr}"
            )
            return {file.name: file.read_bytes() for file in output.glob("*.rs")}

        generated = generate("valid", policy)
        library = generated["sema_lib_gen.rs"]
        assert b"pub fn check_assignment_target(" in library
        assert b"pub fn store_value(" in library
        assert b"pub fn increment_value(" in library
        assert b"impl agsem_runtime::AssignmentPolicyContext for Context" in library
        assert b"crate::sema_lib_gen::store_value(" in generated["sema_gen.rs"]
        assert b"crate::sema_lib_gen::increment_value(" in generated["sema_gen.rs"]
        changed = generate("changed_message", policy.replace(
            "incompatible assignment types", "assignment conversion failed"))
        assert b"assignment conversion failed" in changed["sema_lib_gen.rs"]
        assert library != changed["sema_lib_gen.rs"]

        generate("missing_policy", "", "", "unresolved symbol: check_assignment_target")
        generate("missing_bindings", policy, "", "sema.missing_binding")
        generate("unknown_field", policy.replace("target =", "targets ="),
                 expected="unknown field targets")
        generate("missing_field", policy.replace("        flow = initialized_after_success;\n", ""),
                 expected="missing field flow")
        generate("missing_increment", policy.replace("        increment = increment_value;\n", ""),
                 expected="missing field increment")
        generate("invalid_increment_step", policy.replace(
            "increment_step = integer_one", "increment_step = any_value"),
            expected="unknown value for increment_step")
        generate("bad_value", policy.replace("simple = implicit_conversion", "simple = any_conversion"),
                 expected="unknown value for simple")
        generate("ir_mismatch", policy.replace("ir = Store", "ir = StoreAndCompoundStore"),
                 expected="ir and evaluation disagree")
        generate("unknown_error", policy.replace("invalid_target =", "invalid_targets ="),
                 expected="unknown error field invalid_targets")
        generate("missing_binding", policy, bindings.replace("        flow = current_flow;\n", ""),
                 expected="assignment.store_value.flow: missing explicit binding")
        generate("missing_builder", policy,
                 bindings.replace("        expression_builder = add_expression;\n", ""),
                 expected="assignment.store_value.expression_builder: missing explicit binding")
        generate("missing_integer_literal", policy,
                 bindings.replace("        integer_literal = integer_literal;\n", ""),
                 expected="assignment.store_value.integer_literal: missing explicit binding")
        generate("unknown_binding", policy, bindings.replace("        mark =", "        marks ="),
                 expected="sema.unknown_binding_slot")
        generate("duplicate_policy", policy + policy, expected="duplicate or conflicting declaration: store_value")
        generate("name_collision", policy.replace("check = check_assignment_target", "check = binary_expr"),
                 expected="duplicate or conflicting declaration:")
        generate("duplicate_policy_names", policy.replace("check = check_assignment_target", "check = increment_value"),
                 expected="duplicate or conflicting declaration:")


if __name__ == "__main__":
    main()

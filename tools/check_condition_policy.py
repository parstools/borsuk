#!/usr/bin/env python3
"""Verify condition-policy validation and generated Rust adapters."""

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
    module_start = source.index("    modules {\n")
    module_end = source.index("    rust_context Context;", module_start)
    source = source[:module_start] + source[module_end:]
    start = source.index("    condition_policy condition_bool {")
    middle = source.index("    condition_bindings condition_bool {", start)
    end = source.index("    statement_ir statements {", middle)
    policy = source[start:middle]
    bindings = source[middle:end]
    source = source[:start] + source[end:]

    with tempfile.TemporaryDirectory(prefix="agsem-condition-policy-") as temporary:
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
        assert b"pub fn condition_bool(" in library
        assert b"impl agsem_runtime::ConditionPolicyContext for Context" in library
        assert b"crate::sema_lib_gen::condition_bool(" in generated["sema_gen.rs"]
        changed = generate("changed_message", policy.replace(
            "condition is not convertible to bool", "invalid test expression"))
        assert b"invalid test expression" in changed["sema_lib_gen.rs"]
        assert library != changed["sema_lib_gen.rs"]

        generate("missing_policy", "", "", "unresolved symbol: condition_bool")
        generate("missing_bindings", policy, "", "sema.missing_binding")
        generate("unknown_field", policy.replace("accepted =", "accept ="),
                 expected="unknown field accept")
        generate("missing_field", policy.replace("        already = identity;\n", ""),
                 expected="missing field already")
        generate("invalid_value", policy.replace("accepted = numeric_or_pointer", "accepted = all"),
                 expected="unknown value for accepted")
        generate("unknown_error", policy.replace("incompatible =", "invalid ="),
                 expected="unknown error field invalid")
        generate("missing_binding", policy, bindings.replace("        emit = add_expression;\n", ""),
                 expected="condition.condition_bool.emit: missing explicit binding")
        generate("duplicate_policy", policy + policy,
                 expected="duplicate or conflicting declaration: condition_bool")
        generate("name_collision", policy.replace("condition_bool", "binary_expr"),
                 bindings.replace("condition_bool", "binary_expr"),
                 expected="duplicate or conflicting declaration:")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Verify declarative statement IR mappings and generated constructors."""

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
    start = source.index("    statement_ir statements {")
    middle = source.index("    statement_bindings statements {", start)
    end = source.index("    rust_context Context;", middle)
    policy = source[start:middle]
    bindings = source[middle:end]
    source = source[:start] + source[end:]

    with tempfile.TemporaryDirectory(prefix="agsem-statement-ir-") as temporary:
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
        for name in ("append_statement", "call_statement", "make_if_without_else",
                     "make_if_with_else", "make_while", "for_initialization", "make_for"):
            assert f"pub fn {name}(".encode() in library
            assert f"crate::sema_lib_gen::{name}(".encode() in generated["sema_gen.rs"]
        assert b"Operation::For { scope, initialization, condition, body, update }" in library

        generate("missing_policy", "", "", "unresolved symbol:")
        generate("missing_bindings", policy, "", "sema.missing_binding")
        generate("unknown_field", policy.replace("call_ir =", "calls_ir ="),
                 expected="statement_ir unknown field calls_ir")
        generate("missing_field", policy.replace("        block_ir = Block;\n", ""),
                 expected="statement_ir missing field block_ir")
        generate("duplicate_field", policy.replace("        block_ir = Block;", "        call_ir = Call;"),
                 expected="statement_ir duplicate field call_ir")
        generate("duplicate_name", policy.replace("loop_while = make_while", "loop_while = make_for"),
                 expected="duplicate or conflicting declaration:")
        generate("name_collision", policy.replace("call = call_statement", "call = binary_expr"),
                 expected="duplicate or conflicting declaration:")
        generate("bad_binding", policy, bindings.replace("        emit = add_operation;\n", "", 1),
                 expected="statement.statements.emit: missing explicit binding")
        generate("wrong_binding_name", policy, bindings.replace("statement_bindings statements", "statement_bindings others"),
                 expected="sema.orphan_binding")


if __name__ == "__main__":
    main()

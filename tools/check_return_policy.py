#!/usr/bin/env python3
"""Check parsed return policies without changing existing generated artifacts."""

from coge_cli import MigratedCogeParser, explicit_model_bindings
import subprocess
import tempfile
from pathlib import Path


POLICY = '''    return_policy function_return {
        target = enclosing_function;
        value = absent_for_void_otherwise_required;
        conversion = implicit_conversion;
        cleanup = no_cleanup;
        evaluation = capture_value_before_cleanup;
        flow = unreachable_after_success;
        ir = Return;
        errors {
            missing_target = "return outside function";
            wrong_presence = "invalid return value";
            failed_conversion = "incompatible return type";
        }
    }
'''

BINDINGS = '''    model_bindings function_return {
        active_function = active_function;
        functions = functions;
        flow = current_flow;
        poison = poisoned_expression;
        convert = convert;
        emit = add_operation;
        return_ir = tuple;
        cleanup_scopes = no_scopes;
    }
'''


def main() -> None:
    arguments = MigratedCogeParser()
    arguments.add_argument("--coge", "--sema", dest="coge", help="coge binary (--sema is a compatibility alias)", type=Path, required=True)
    arguments.add_argument("--source", type=Path, required=True)
    arguments.add_argument("--contracts", type=Path, required=True)
    args = arguments.parse_args()
    source = explicit_model_bindings(args.source)
    assert POLICY in source
    assert BINDINGS in source
    source = source.replace(POLICY, "", 1)
    source = source.replace(BINDINGS, "", 1)

    with tempfile.TemporaryDirectory(prefix="agsem-return-policy-") as temp:
        root = Path(temp)

        def generate(name: str, policy: str, expected: str | None = None,
                     bindings: str = BINDINGS) -> dict[str, bytes]:
            path = root / f"{name}.coge"
            path.write_text(source.replace("semantic_model {\n", "semantic_model {\n" + policy + bindings, 1))
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
            assert completed.returncode == 0, f"{name}: {completed.stdout}{completed.stderr}"
            return {file.name: file.read_bytes() for file in output.glob("*.rs")}

        generated = generate("valid", POLICY)
        assert b"pub fn function_return(" in generated["sema_lib_gen.rs"]
        assert b"crate::sema_lib_gen::function_return(" in generated["sema_gen.rs"]
        changed = generate(
            "changed_message",
            POLICY.replace("incompatible return type", "return conversion failed"),
        )
        assert b'return conversion failed' in changed["sema_lib_gen.rs"]
        assert generated["sema_lib_gen.rs"] != changed["sema_lib_gen.rs"]
        generate("missing_policy", "", "unresolved symbol: function_return", "")
        generate("missing_bindings", POLICY, "sema.missing_binding", "")
        generate("invalid_shape", POLICY, "return_ir requires tuple or record",
                 BINDINGS.replace("return_ir = tuple", "return_ir = unknown"))
        generate("cleanup_mismatch",
                 POLICY.replace("cleanup = no_cleanup", "cleanup = exited_automatic_lifetimes"),
                 "cleanup_scopes disagree")
        generate("missing_binding", POLICY, "return.function_return.flow: missing explicit binding",
                 BINDINGS.replace("        flow = current_flow;\n", ""))
        generate("unknown_binding", POLICY, "sema.unknown_binding_slot",
                 BINDINGS.replace("        flow =", "        state ="))
        generate("unknown_field", POLICY.replace("flow =", "flows ="), "unknown field flows")
        generate("duplicate_field", POLICY.replace("        ir =", "        target = enclosing_function;\n        ir ="), "duplicate field target")
        generate("missing_field", POLICY.replace("        flow = unreachable_after_success;\n", ""), "missing field flow")
        generate("unknown_value", POLICY.replace("implicit_conversion", "any_conversion"), "unknown value for conversion")
        generate("missing_errors", POLICY[:POLICY.index("        errors {")] + "    }\n", "missing errors block")
        generate("unknown_error", POLICY.replace("wrong_presence =", "wrong_presences ="), "unknown error field wrong_presences")
        generate("duplicate_error", POLICY.replace("            failed_conversion =", "            wrong_presence = \"again\";\n            failed_conversion ="), "duplicate error field wrong_presence")
        generate("duplicate_policy", POLICY + POLICY, "duplicate or conflicting declaration: function_return")
        generate("multiple_policies", POLICY + POLICY.replace("function_return", "another_return"),
                 "only one return_policy is supported")
        generate("function_collision", POLICY.replace("function_return", "store_value"),
                 "duplicate or conflicting declaration:", BINDINGS.replace("function_return", "store_value"))


if __name__ == "__main__":
    main()

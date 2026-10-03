#!/usr/bin/env python3
"""Verify flow action mappings and generated analysis helpers."""

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
    start = source.index("    flow_actions statements {")
    middle = source.index("    flow_bindings statements {", start)
    end = source.index("    rust_context Context;", middle)
    actions = source[start:middle]
    bindings = source[middle:end]
    source = source[:start] + source[end:]

    with tempfile.TemporaryDirectory(prefix="agsem-flow-actions-") as temporary:
        root = Path(temporary)

        def generate(name: str, actions_text: str, bindings_text: str = bindings,
                     expected: str | None = None) -> dict[str, bytes]:
            path = root / f"{name}.coge"
            path.write_text(source.replace(
                "semantic_model {\n",
                "semantic_model {\n" + actions_text + bindings_text,
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

        generated = generate("valid", actions)
        library = generated["sema_lib_gen.rs"]
        analysis = generated["sema_gen.rs"]
        for name in ("snapshot_flow", "restore_flow", "merge_flow_snapshot",
                     "recover_statement"):
            assert f"pub fn {name}(".encode() in library
            assert f"crate::sema_lib_gen::{name}(".encode() in analysis
        assert b"agsem_runtime::snapshot_flow(&ctx.current_flow, &mut ctx.flow_snapshots)" in library
        assert b"agsem_runtime::restore_flow(&ctx.flow_snapshots, snapshot.0)" in library
        assert b"agsem_runtime::merge_flow_snapshot(" in library
        assert b"ctx.record_error(message, source)" in library
        assert b"Operation::Error" in library

        generate("missing_bindings", actions, "", "sema.missing_binding")
        generate("unknown_field", actions.replace("snapshot =", "capture ="),
                 expected="flow_actions unknown field capture")
        generate("missing_field", actions.replace("        recover = recover_statement;\n", ""),
                 expected="flow_actions missing field recover")
        generate("duplicate_name", actions.replace("restore = restore_flow", "restore = snapshot_flow"),
                 expected="duplicate or conflicting declaration:")
        generate("name_collision", actions.replace("snapshot = snapshot_flow", "snapshot = binary_expr"),
                 expected="duplicate or conflicting declaration:")
        generate("bad_binding", actions, bindings.replace("        emit = add_operation;\n", ""),
                 expected="flow.statements.emit: missing explicit binding")
        generate("wrong_binding_name", actions, bindings.replace("flow_bindings statements", "flow_bindings other"),
                 expected="sema.orphan_binding")


if __name__ == "__main__":
    main()

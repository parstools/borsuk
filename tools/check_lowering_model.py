#!/usr/bin/env python3
"""Verify the first generated Core IR lowering contract for ToyC."""

from coge_cli import MigratedCogeParser
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    parser = MigratedCogeParser()
    parser.add_argument("--coge", "--sema", dest="coge", help="coge binary (--sema is a compatibility alias)", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--contracts", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.read_text()
    start = source.index("lowering_model {\n")
    backend = source.index("backend_c {\n", start)
    end = source.index("execution_model {\n", backend)
    explicit = args.source.with_suffix(".sema").read_text()
    explicit_start = explicit.index("lowering_model {\n")
    explicit_end = explicit.index("backend_c {\n", explicit_start)
    lowering = explicit[explicit_start:explicit_end]
    source = source[:start] + source[end:]

    with tempfile.TemporaryDirectory(prefix="agsem-lowering-model-") as temporary:
        root = Path(temporary)

        def generate(name: str, definition: str, expected: str | None = None) -> bytes:
            path = root / f"{name}.coge"
            path.write_text(source.replace("execution_model {\n",
                                           definition + "execution_model {\n", 1))
            output = root / name
            completed = subprocess.run(
                [str(args.coge.resolve()), "--contracts", str(args.contracts.resolve()), "--emit-rust-dir", str(output), str(path)],
                capture_output=True, text=True, check=False,
            )
            if expected:
                assert completed.returncode != 0 and expected in completed.stderr, (
                    f"{name}: expected {expected!r}\n{completed.stdout}{completed.stderr}"
                )
                return b""
            assert completed.returncode == 0, (
                f"{name}: {completed.stdout}{completed.stderr}"
            )
            result = output / "lowering_gen.rs"
            return result.read_bytes() if result.exists() else b""

        generated = generate("valid", lowering)
        assert b"pub fn lower_function_to_structured(" in generated
        assert b"structured::lower_to_cfg(&module)" in generated
        assert b"structured::Statement::Return" in generated
        assert b"core::InstructionKind::Binary" in generated
        assert b"operation: core::BinaryOp::Add" in generated
        assert b"operation: core::BinaryOp::Subtract" in generated
        assert b"operation: core::BinaryOp::Multiply" in generated
        assert b"operation: core::BinaryOp::Divide" in generated
        assert b"core::InstructionKind::SlotAddress" in generated
        assert b"core::InstructionKind::Load" in generated
        assert b"core::InstructionKind::Store" in generated
        assert b"core::InstructionKind::Compare" in generated
        assert b"core::InstructionKind::Call" in generated
        assert b"core::InstructionKind::IndexAddress" in generated
        assert b"core::InstructionKind::BoundsCheck" in generated
        assert b"core::InstructionKind::ResetArray" in generated
        assert b"core::InstructionKind::Convert" in generated
        assert b"core::Constant::F32Bits" in generated
        assert b"core::Constant::U8" in generated
        assert b"fn reachable_functions(" in generated
        assert b"structured::Statement::If" in generated
        assert b"structured::Statement::While" in generated
        assert b"structured::Statement::For" in generated
        assert generate("absent", "") == b""
        generate("unknown", lowering.replace("add_ir =", "sum_ir ="),
                 "lowering_model unknown field sum_ir")
        generate("missing", lowering.replace("    return_ir = Return;\n", ""),
                 "lowering_model missing field return_ir")
        generate("duplicate", lowering.replace("    add_ir = Add;",
                                               "    add_ir = Add;\n    add_ir = Add;"),
                 "lowering_model duplicate field add_ir")
        generate("name_collision", lowering.replace(
            "lower = lower_function_to_structured", "lower = lower_expression"),
            "lowering_model function name conflicts with helper")

        stale = root / "stale.coge"
        stale.write_text(source.replace("execution_model {\n",
                                        lowering + "execution_model {\n", 1))
        output = root / "reused"
        command = [str(args.coge.resolve()), "--contracts", str(args.contracts.resolve()), "--emit-rust-dir", str(output), str(stale)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        assert (output / "lowering_gen.rs").exists()
        stale.write_text(source)
        subprocess.run(command, check=True, capture_output=True, text=True)
        assert not (output / "lowering_gen.rs").exists()

        foreign = root / "foreign"
        foreign.mkdir()
        (foreign / "lowering_gen.rs").write_text("user-owned\n")
        command = [str(args.coge.resolve()), "--contracts", str(args.contracts.resolve()), "--emit-rust-dir", str(foreign), str(stale)]
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        assert completed.returncode != 0
        assert "refusing to remove unowned output:" in completed.stderr
        assert (foreign / "lowering_gen.rs").read_text() == "user-owned\n"


if __name__ == "__main__":
    main()

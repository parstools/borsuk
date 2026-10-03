#!/usr/bin/env python3
"""Verify the generated C backend adapter and its specification checks."""

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
    start = source.index("backend_c {\n")
    end = source.index("execution_model {\n", start)
    explicit = args.source.with_suffix(".sema").read_text()
    explicit_start = explicit.index("backend_c {\n")
    explicit_end = explicit.index("execution_model {\n", explicit_start)
    backend = explicit[explicit_start:explicit_end]
    source = source[:start] + source[end:]

    with tempfile.TemporaryDirectory(prefix="agsem-backend-c-") as temporary:
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
            result = output / "backend_c_gen.rs"
            return result.read_bytes() if result.exists() else b""

        generated = generate("valid", backend)
        assert b"pub fn emit_c(" in generated
        assert b"crate::lowering_gen::lower_function_to_structured" in generated
        assert b"agsem_runtime::c_backend::emit_c(&ir)" in generated
        assert generate("absent", "") == b""
        generate("unknown", backend.replace("emit =", "write ="),
                 "backend_c unknown field write")
        generate("missing", backend.replace("    emit = emit_c;\n", ""),
                 "backend_c missing field emit")
        generate("mismatch", backend.replace(
            "lower = lower_function_to_structured", "lower = other_lower"),
            "backend_c lower differs from lowering_model")

        stale = root / "stale.coge"
        stale.write_text(source.replace("execution_model {\n",
                                        backend + "execution_model {\n", 1))
        output = root / "reused"
        command = [str(args.coge.resolve()), "--contracts", str(args.contracts.resolve()), "--emit-rust-dir", str(output), str(stale)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        assert (output / "backend_c_gen.rs").exists()
        stale.write_text(source)
        subprocess.run(command, check=True, capture_output=True, text=True)
        assert not (output / "backend_c_gen.rs").exists()


if __name__ == "__main__":
    main()

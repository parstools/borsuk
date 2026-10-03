#!/usr/bin/env python3
"""Check execution-model errors before invoking the Rust compiler."""

from coge_cli import MigratedCogeParser
import subprocess
from pathlib import Path


def main() -> None:
    parser = MigratedCogeParser()
    parser.add_argument("--coge", "--sema", dest="coge", help="coge binary (--sema is a compatibility alias)", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--contracts", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.read_text()
    # Keep testing body validation without a profile signature mismatch hiding
    # the mutation/capture error. Legacy fixtures retain the explicit bodies.
    if '    execution_profile shared_value_v1;\n' in source:
        legacy = args.source.with_suffix('.sema').read_text()
        begin = source.index('execution_model {\n')
        end = source.index('generation {\n', begin)
        legacy_begin = legacy.index('execution_model {\n')
        legacy_end = legacy.index('options {\n', legacy_begin)
        source = source[:begin] + legacy[legacy_begin:legacy_end] + source[end:]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    cases = [
        ("readonly_assignment",
         "function tick(source: SourceRange) -> Result<Unit, RuntimeError> mutates",
         "function tick(source: SourceRange) -> Result<Unit, RuntimeError>",
         "runtime assignment needs mutates"),
        ("readonly_store",
         "function store(place: PlaceId, value: Value, source: SourceRange) -> Result<Unit, RuntimeError> mutates",
         "function store(place: PlaceId, value: Value, source: SourceRange) -> Result<Unit, RuntimeError>",
         "runtime mutation needs mutates: write_path"),
        ("capture_value", "capture(execute(body))", "capture(body)",
         "capture needs one fallible action call"),
        ("capture_expression", "capture(execute(body))", "capture(execute(body) + execute(body))",
         "capture needs one fallible action call"),
        ("property_domain", "forall a: I32", "forall a: F32",
         "forall domain must be I32"),
        ("property_oracle", "== mathematical_sum(a, b)", "== invented_oracle(a, b)",
         "unknown property oracle: invented_oracle"),
        ("property_function", "expect checked_add_i32(a, b)", "expect unknown_add(a, b)",
         "unknown property function: unknown_add"),
        ("property_nested_math", "== mathematical_sum(a, b)", "== mathematical_sum(mathematical_sum(a, b), b)",
         "property oracle operands must be I32 scalars"),
    ]
    for name, old, new, expected in cases:
        assert old in source, f"fixture no longer contains {old!r}"
        path = args.output_dir / f"{name}.coge"
        path.write_text(source.replace(old, new, 1))
        result = subprocess.run(
            [str(args.coge.resolve()), "--contracts", str(args.contracts.resolve()), "--emit-rust-dir", str(args.output_dir / name), str(path)],
            text=True, capture_output=True, check=False,
        )
        assert result.returncode != 0 and expected in result.stderr, (
            f"{name}: expected {expected!r}\n{result.stdout}{result.stderr}"
        )
    print("Execution model: rejected read-only state mutations and invalid capture")


if __name__ == "__main__":
    main()

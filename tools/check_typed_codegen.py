#!/usr/bin/env python3
"""Verify typed analyzer contracts drive Rust signatures and calls."""

import argparse
from pathlib import Path
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--coge", "--sema", dest="sema", help="coge binary (legacy adapter; --sema is a compatibility alias)", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--expected", type=Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [args.sema, "--legacy", "--emit-rust-dir", str(args.output_dir), str(args.source)],
        check=True,
    )
    generated = (args.output_dir / "sema_gen.rs").read_text()
    if generated != args.expected.read_text():
        raise AssertionError("typed Rust output differs from checked-in source")
    for fragment in (
        "use crate::CalcContext;",
        "ctx: &mut CalcContext,\n    seed: i64,\n    offset: i64,",
        ") -> Result<i64, &'static str>",
        "analyze_program_alt_0(ctx, seed, offset, node)",
        "analyze_value(&mut *ctx, seed, offset, operand)?",
    ):
        if fragment not in generated:
            raise AssertionError(f"missing typed Rust fragment: {fragment}")
    if "FlowId" in generated or "ScopeId" in generated:
        raise AssertionError("typed Rust output retained Scope-specific types")


if __name__ == "__main__":
    main()

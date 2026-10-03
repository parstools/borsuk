"""Verify every migrated author source in an isolated directory against Rust baselines."""

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    for name in ("sema", "coge", "ag", "repo"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    rustfmt = shutil.which("rustfmt")
    if not rustfmt:
        parser.error("rustfmt is required for Rust baseline comparison")

    def run(binary, *arguments, cwd):
        result = subprocess.run(
            [str(binary), *map(str, arguments)], cwd=cwd,
            capture_output=True, text=True,
        )
        assert result.returncode == 0, result.stdout + result.stderr

    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def compare_rust(generated, expected, root):
        actual = {file.name: file for file in generated.glob("*_gen.rs")}
        baseline = {file.name: file for file in expected.glob("*_gen.rs")}
        assert actual.keys() == baseline.keys(), (actual.keys(), baseline.keys())
        for name, file in actual.items():
            if file.read_bytes() == baseline[name].read_bytes():
                continue
            assert name != "parser_gen.rs", "parser differs: " + str(file)
            copies = [root / (prefix + name) for prefix in ("actual-", "expected-")]
            shutil.copyfile(file, copies[0])
            shutil.copyfile(baseline[name], copies[1])
            run(rustfmt, "--edition", "2021", "--config", "skip_children=true", *copies, cwd=root)
            assert copies[0].read_bytes() == copies[1].read_bytes(), "Rust differs: " + str(file)
        assert (generated / "sema_modules.manifest").read_bytes() == (
            expected / "sema_modules.manifest"
        ).read_bytes(), "module ownership differs"

    names = ("toyc", "toycp", "toyscope_1", "toyscope_2", "toyscope_3")
    with tempfile.TemporaryDirectory(prefix="agsem-migration-") as directory:
        for name in names:
            root = Path(directory) / name
            root.mkdir()
            source = root / (name + ".coge")
            contract = root / "runtime.json"
            language = "toyscope" if name.startswith("toyscope_") else name
            source_file = args.repo / ("sema/examples" if language == "toyscope" else "coge/examples") / name / (name + ".coge")
            shutil.copyfile(source_file, source)
            shutil.copyfile(args.repo / "contracts" / (language + "-runtime-v1.json"), contract)
            assert sorted(file.name for file in root.iterdir()) == ["runtime.json", source.name]
            semantic_only = language == "toyscope"
            if not semantic_only:
                run(args.coge, "--check", "--contracts", contract, source, cwd=root)
            exported = root / (name + ".sema")
            grammar = root / (name + ".ag")
            run(args.coge, "--emit-sema", exported, "--contracts", contract, source, cwd=root)
            run(args.coge, "--emit-ag", grammar, source, cwd=root)
            for output in (exported, grammar):
                metadata = json.loads(Path(str(output) + ".provenance.json").read_text())
                assert metadata["source_sha256"] == digest(source)
                assert metadata["output_sha256"] == digest(output)
                assert metadata["tool_sha256"] == digest(args.coge)
                if output == exported:
                    assert metadata["contracts"][0]["sha256"] == digest(contract)
            example_dir = args.repo / ("sema/examples" if semantic_only else "coge/examples") / name
            saved_sema = (example_dir / exported.name if semantic_only else
                          example_dir / "generated/projections" / exported.name)
            assert exported.read_bytes() == saved_sema.read_bytes(), "saved sema differs"
            assert grammar.read_bytes() == (
                example_dir / "generated/projections" / grammar.name
            ).read_bytes(), "saved Ag differs"
            generated = root / "generated"
            if not semantic_only:
                run(args.coge, "--emit-rust-dir", generated, "--contracts", contract, source, cwd=root)
            source.unlink()
            run(args.sema, "--check", "--contracts", contract, exported, cwd=root)
            semantic_dir = generated if semantic_only else root / "semantic-only"
            run(args.sema, "--emit-rust-dir", semantic_dir, "--contracts", contract, exported, cwd=root)
            assert not (semantic_dir / "interpreter_gen.rs").exists(), "sema emitted execution"
            if not semantic_only:
                for file in semantic_dir.glob("*_gen.rs"):
                    assert file.read_bytes() == (generated / file.name).read_bytes(), file
            via_sema = root / "via-sema.ag"
            run(args.sema, "--emit-ag", via_sema, exported, cwd=root)
            assert grammar.read_bytes() == via_sema.read_bytes(), "projection law failed"
            run(args.ag, "--emit-rust-parser", generated / "parser_gen.rs", grammar, cwd=root)
            compare_rust(generated, example_dir / "generated", root)
            print(name + ": isolated parser, analysis, available execution and all Rust baselines OK")


if __name__ == "__main__":
    main()

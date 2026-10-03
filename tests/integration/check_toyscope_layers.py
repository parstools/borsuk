"""Check typed ToyScope analysis exports independently of legacy execution."""

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    for name in ("sema", "coge", "ag", "repo"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()

    def run(binary, *arguments, error=None):
        result = subprocess.run(
            [str(binary), *map(str, arguments)], capture_output=True, text=True
        )
        if error:
            assert result.returncode != 0 and error in result.stderr, result.stderr
        else:
            assert result.returncode == 0, result.stdout + result.stderr

    with tempfile.TemporaryDirectory(prefix="agsem-toyscope-layers-") as directory:
        root = Path(directory)
        contract = root / "runtime.json"
        shutil.copyfile(args.repo / "contracts/toyscope-runtime-v1.json", contract)
        for number in (1, 2, 3):
            name = f"toyscope_{number}"
            source = root / (name + ".coge")
            exported = root / (name + ".sema")
            example_dir = args.repo / "sema/examples" / name
            shutil.copyfile(example_dir / (name + ".coge"), source)
            run(args.coge, "--emit-sema", exported, "--contracts", contract, source)
            assert exported.read_bytes() == (example_dir / exported.name).read_bytes()
            run(args.sema, "--contracts", contract, source, error="document.wrong_kind")
            # The retained execution contract still needs conversion to the
            # supported execution model; analysis export remains usable.
            run(args.coge, "--check", "--contracts", contract, source,
                error="unsupported execution statement: requireStatement")
            source.unlink()
            before = set(root.iterdir())
            run(args.sema, "--check", "--contracts", contract, exported)
            assert before == set(root.iterdir()), "check wrote files"
            grammar = root / (name + ".ag")
            run(args.sema, "--emit-ag", grammar, exported)
            assert grammar.read_bytes() == (
                example_dir / "generated/projections" / grammar.name
            ).read_bytes()
            run(args.ag, "--check", grammar)
            print(name + ": independent analysis and explicit execution conversion OK")


if __name__ == "__main__":
    main()

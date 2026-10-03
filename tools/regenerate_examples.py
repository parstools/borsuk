#!/usr/bin/env python3
"""Regenerate Rust artifacts in generated/ beside each example's src/."""

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_FILE = re.compile(r"sema_[A-Za-z_][A-Za-z_0-9]*_gen\.rs")
SOURCES = {
    "scope_codegen": ("sema/examples/scope_codegen/scope_codegen.sema", "sema/examples/scope_codegen/scope_codegen.ag"),
    "typed_codegen": ("sema/examples/typed_codegen/typed_codegen.sema", None),
    "typed_mixed_codegen": ("sema/examples/typed_mixed_codegen/typed_mixed.sema", "sema/examples/typed_mixed_codegen/typed_mixed.ag"),
    "toyscope_1": ("sema/examples/toyscope_1/toyscope_1.coge", None),
    "toyscope_2": ("sema/examples/toyscope_2/toyscope_2.coge", None),
    "toyscope_3": ("sema/examples/toyscope_3/toyscope_3.coge", None),
    "toyc": ("coge/examples/toyc/toyc.coge", None),
    "toycp": ("coge/examples/toycp/toycp.coge", None),
}
CONTRACTS = {
    "toyc": "contracts/toyc-runtime-v1.json",
    "toycp": "contracts/toycp-runtime-v1.json",
    "toyscope_1": "contracts/toyscope-runtime-v1.json",
    "toyscope_2": "contracts/toyscope-runtime-v1.json",
    "toyscope_3": "contracts/toyscope-runtime-v1.json",
}
SEMANTIC_ONLY = {"toyscope_1", "toyscope_2", "toyscope_3"}


def module_names(manifest: Path) -> set[str]:
    if not manifest.exists():
        return set()
    lines = manifest.read_text().splitlines()
    if not lines or lines[0] != "agsem-modules-v1":
        raise ValueError(f"invalid generated module manifest: {manifest}")
    names = lines[1:]
    if len(names) != len(set(names)) or any(
        not MODULE_FILE.fullmatch(name) or name == "sema_lib_gen.rs"
        for name in names
    ):
        raise ValueError(f"invalid generated module manifest: {manifest}")
    return set(names)


def main() -> None:
    parser = argparse.ArgumentParser(epilog=(
        "Migration: ToyC/ToyCP use standalone .coge sources and explicit runtime contracts. "
        "--sema PATH is a compatibility alias for --coge PATH and expects the coge binary. "
        "Use --sema-cli PATH for the semantic-only binary used by typed ToyScope. "
        "See docs/GENERATED.md."
    ))
    parser.add_argument("examples", nargs="*", help="Example names; default: all")
    parser.add_argument("--coge", "--sema", dest="coge", help="coge binary (--sema is a compatibility alias)", type=Path, default=ROOT / "build/bin/coge")
    parser.add_argument("--agas", type=Path, default=ROOT / "build/bin/agas")
    parser.add_argument("--sema-cli", type=Path, default=ROOT / "build/bin/sema",
                        help="semantic-only CLI used by typed ToyScope")
    args = parser.parse_args()
    names = args.examples or list(SOURCES)
    for name in names:
        if name not in SOURCES:
            parser.error(f"unknown example: {name}")
    for name in names:
        source, grammar = SOURCES[name]
        source_path = ROOT / source
        contracts = CONTRACTS.get(name)
        options = (["--contracts", str(ROOT / contracts)]
                   if contracts else ["--legacy"])
        with tempfile.TemporaryDirectory(prefix="agsem-regenerate-") as directory:
            temporary = Path(directory)
            if contracts:
                for extension in ("sema", "ag"):
                    projection = temporary / (name + "." + extension)
                    subprocess.run(
                        [str(args.coge.resolve()), "--emit-" + extension,
                         str(projection), *(options if extension == "sema" else []),
                         str(source_path)],
                        cwd=ROOT, check=True,
                    )
                grammar = str(temporary / (name + ".ag"))
            emitter = args.sema_cli if name in SEMANTIC_ONLY else args.coge
            rust_source = temporary / (name + ".sema") if name in SEMANTIC_ONLY else source_path
            subprocess.run(
                [str(emitter.resolve()), *options, "--emit-rust-dir", directory, str(rust_source)],
                cwd=ROOT, check=True,
            )
            files = sorted(temporary.glob("*_gen.rs"))
            if grammar:
                output = temporary / "parser_gen.rs"
                subprocess.run(
                [str(args.agas.resolve()), "--emit-rust-parser", str(output), str(ROOT / grammar)],
                    check=True,
                )
                files.append(output)
            destination = ROOT / source_path.parent.relative_to(ROOT) / "generated"
            destination.mkdir(parents=True, exist_ok=True)
            generated_manifest = temporary / "sema_modules.manifest"
            if not generated_manifest.exists():
                raise RuntimeError("sema generator did not write the module manifest")
            if (destination / "sema_modules.manifest").is_symlink():
                raise ValueError("module manifest must not be a symlink")
            old_modules = module_names(destination / "sema_modules.manifest")
            new_modules = module_names(generated_manifest)
            for module in new_modules - old_modules:
                if (destination / module).exists():
                    raise ValueError(f"refusing to overwrite unowned module: {module}")
            for module in new_modules:
                if (destination / module).is_symlink():
                    raise ValueError(f"module output must not be a symlink: {module}")
            for module in old_modules - new_modules:
                (destination / module).unlink(missing_ok=True)
            for file_name, header in (
                ("lowering_gen.rs", "// Generated by sema from lowering_model.\n"),
                ("backend_c_gen.rs", "// Generated by sema from backend_c.\n"),
                ("backend_llvm_gen.rs", "// Generated by sema from backend_llvm.\n"),
            ):
                output = destination / file_name
                if not any(file.name == file_name for file in files) and (
                    output.exists() or output.is_symlink()
                ):
                    if output.is_symlink() or not output.read_text().startswith(header):
                        raise ValueError(f"refusing to remove unowned output: {file_name}")
                    output.unlink()
            for file in files:
                shutil.copyfile(file, destination / file.name)
            shutil.copyfile(generated_manifest, destination / "sema_modules.manifest")
            provenance = temporary / "sema_generation.provenance.json"
            if provenance.exists():
                target = destination / provenance.name
                if target.is_symlink():
                    raise ValueError(f"generation provenance must not be a symlink: {target}")
                shutil.copyfile(provenance, target)
            if contracts:
                projections = destination / "projections"
                projections.mkdir(parents=True, exist_ok=True)
                for extension in ("sema", "ag"):
                    projection = temporary / (name + "." + extension)
                    for artifact in (projection, Path(str(projection) + ".provenance.json")):
                        target = ((source_path.parent / artifact.name)
                                  if name in SEMANTIC_ONLY and extension == "sema"
                                  else projections / artifact.name)
                        if target.is_symlink():
                            raise ValueError(f"projection output must not be a symlink: {target}")
                        shutil.copyfile(artifact, target)
            print(f"{name}: {len(files)} generated files", flush=True)


if __name__ == "__main__":
    main()

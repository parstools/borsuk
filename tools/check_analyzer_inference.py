#!/usr/bin/env python3
"""Exercise inferred interfaces against independently declared interfaces."""

import argparse
import re
import subprocess
from pathlib import Path


def compact(source: str) -> str:
    attributes = {}
    pattern = r"^    analyzer \w+\((.*?)\) -> [^;]+;\n"
    for parameters in re.findall(pattern, source, re.MULTILINE):
        for parameter in parameters.split(","):
            if parameter.strip():
                name, annotation = map(str.strip, parameter.split(":", 1))
                if name in attributes and attributes[name] != annotation:
                    raise AssertionError(f"inconsistent fixture attribute {name}")
                attributes[name] = annotation
    source = re.sub(pattern, "", source, flags=re.MULTILINE)
    declarations = "".join(
        f"    inherited {name}: {annotation};\n"
        for name, annotation in attributes.items()
    )
    return source.replace("semantic_model {\n", "semantic_model {\n" + declarations, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--coge", "--sema", dest="sema", help="coge binary (legacy adapter; --sema is a compatibility alias)", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    def fixture(name: str) -> str:
        source = (args.source_dir / f"{name}.sema").read_text()
        grammar = (args.source_dir / f"{name}.ag").resolve().as_posix()
        return source.replace(f'for "{name}.ag"', f'for "{grammar}"', 1)

    def generate(name: str, source: str, error: str | None = None) -> dict[str, bytes]:
        path = args.output_dir / f"{name}.sema"
        path.write_text(source)
        output = args.output_dir / name
        completed = subprocess.run(
            [str(args.sema.resolve()), "--legacy", "--emit-rust-dir", str(output), str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        if error is not None:
            if completed.returncode == 0 or error not in completed.stderr:
                raise AssertionError(f"{name}: expected {error!r}\n{completed.stdout}{completed.stderr}")
            return {}
        if completed.returncode:
            raise AssertionError(f"{name}: {completed.stdout}{completed.stderr}")
        return {file.name: file.read_bytes() for file in output.glob("*.rs")}

    declared = fixture("typed_codegen")
    inferred = compact(declared)
    expected = generate("declared", declared)
    assert generate("inferred", inferred) == expected

    partial = inferred.replace(
        "semantic_model {", "semantic_model {\n    analyzer program(seed: Int, offset: Int) -> Int;", 1
    )
    assert generate("partial", partial) == expected

    shadowed = inferred.replace("semantic_model {", "semantic_model {\n    inherited parsed: Bool;", 1)
    assert generate("shadowed", shadowed) == expected

    implicit = inferred.replace(" with offset: offset, seed: seed", "")
    assert generate("implicit", implicit) == generate(
        "implicit_declared", declared.replace(" with offset: offset, seed: seed", "")
    )

    # Named inputs remain in the interface even when a body ignores them.
    unused = "result = sum + offset;"
    assert generate("unused_inferred", inferred.replace(unused, "result = sum;")) == generate(
        "unused_declared", declared.replace(unused, "result = sum;")
    )

    # Grammar and action cycles propagate inherited requirements and results.
    scope = fixture("scope_codegen")
    assert generate("scope_inferred", compact(scope)) == generate("scope_declared", scope)

    generate("unknown_name", inferred.replace("parsed + seed", "parsed + seedd"), "unbound identifier")
    generate("unknown_argument", inferred.replace("offset: offset", "offste: offset"), "unknown inherited attribute")
    generate("missing_attribute", inferred.replace("    inherited offset: Int;\n", ""), "unknown inherited attribute")
    generate("wrong_type", inferred.replace("inherited offset: Int", "inherited offset: Bool"), "inferred type mismatch")
    generate("duplicate", inferred.replace("semantic_model {", "semantic_model {\n    inherited seed: Int;", 1), "duplicate inherited attribute")
    generate("wrong_hint", partial.replace("offset: Int) -> Int", "offset: Int) -> Bool"), "inferred type mismatch")
    generate(
        "conflicting_branches",
        inferred.replace(unused, "if true { result = sum + offset; } else { result = true; }"),
        "inferred type mismatch",
    )
    generate("ambiguous", inferred.replace(unused, "result = none;"), "cannot infer analyzer result")
    print("Analyzer inference: equivalent Rust, recursive propagation and rejection checks passed")


if __name__ == "__main__":
    main()

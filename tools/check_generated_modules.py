#!/usr/bin/env python3
"""Verify module assignments, stable output and manifest-owned cleanup."""

from coge_cli import MigratedCogeParser
import subprocess
import tempfile
from pathlib import Path


def main() -> None:
    arguments = MigratedCogeParser()
    arguments.add_argument("--coge", "--sema", dest="coge", help="coge binary (--sema is a compatibility alias)", type=Path, required=True)
    arguments.add_argument("--source", type=Path, required=True)
    arguments.add_argument("--contracts", type=Path, required=True)
    args = arguments.parse_args()
    source = args.source.read_text()
    start = source.index("    modules {\n")
    end = source.index("    rust_context Context;", start)
    module = source[start:end]
    source = source[:start] + source[end:]

    with tempfile.TemporaryDirectory(prefix="agsem-modules-") as temporary:
        root = Path(temporary)

        def generate(name: str, declaration: str, output: Path,
                     expected: str | None = None) -> None:
            path = root / f"{name}.coge"
            path.write_text(source.replace(
                "semantic_model {\n", "semantic_model {\n" + declaration, 1
            ))
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
            else:
                assert completed.returncode == 0, (
                    f"{name}: {completed.stdout}{completed.stderr}"
                )

        output = root / "generated"
        generate("valid", module, output)
        child = output / "sema_statements_gen.rs"
        assert child.exists()
        assert (output / "sema_modules.manifest").read_text() == (
            "agsem-modules-v1\nsema_statements_gen.rs\n"
        )
        analyzer = (output / "sema_gen.rs").read_text()
        library = (output / "sema_lib_gen.rs").read_text()
        assert 'pub(crate) mod statements;' in analyzer
        assert 'pub use statements::{analyze_for_statement' in analyzer
        assert 'pub use crate::sema_gen::statements::{' in library
        assert 'pub fn analyze_if_statement(' in child.read_text()
        assert 'pub fn make_if_with_else(' in child.read_text()
        assert 'pub fn analyze_if_statement(' not in analyzer
        before = {file.name: file.read_bytes() for file in output.iterdir()}
        generate("repeated", module, output)
        assert before == {file.name: file.read_bytes() for file in output.iterdir()}
        generate("removed", "", output)
        assert not child.exists()
        assert (output / "sema_modules.manifest").read_text() == "agsem-modules-v1\n"

        unowned = root / "unowned"
        unowned.mkdir()
        (unowned / child.name).write_text("user file\n")
        generate("unowned", module, unowned, "refusing to overwrite unowned module")
        assert (unowned / child.name).read_text() == "user file\n"

        outside = root / "outside.txt"
        outside.write_text("keep this file\n")
        linked = root / "linked"
        linked.mkdir()
        (linked / child.name).symlink_to(outside)
        generate("linked", module, linked, "output must not be a symlink")
        assert outside.read_text() == "keep this file\n"
        linked_manifest = root / "linked_manifest"
        linked_manifest.mkdir()
        (linked_manifest / "sema_modules.manifest").symlink_to(outside)
        generate("linked_manifest", module, linked_manifest,
                 "output must not be a symlink")
        assert outside.read_text() == "keep this file\n"

        generate("unknown_rule", module.replace("ifStatement", "unknownStatement", 1),
                 root / "unknown_rule", "modules unknown rule: unknownStatement")
        generate("unknown_function", module.replace("make_while", "unknown_function", 1),
                 root / "unknown_function", "unresolved symbol: unknown_function")
        generate("duplicate_owner", """    modules {
        first { rules = [ifStatement]; }
        second { rules = [ifStatement]; }
    }
""", root / "duplicate_owner", "modules duplicate owner for ifStatement")
        generate("duplicate_module", """    modules {
        statements { rules = [ifStatement]; }
        statements { rules = [whileStatement]; }
    }
""", root / "duplicate_module", "modules duplicate module: statements")
        generate("reserved_name", module.replace("statements {", "lib {", 1),
                 root / "reserved_name", "modules name collides")
        generate("empty_module", "    modules { unusedmodule {} }\n",
                 root / "empty_module",
                 "modules entry needs rules or functions: unusedmodule")


if __name__ == "__main__":
    main()

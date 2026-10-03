"""Verify actionable migration messages and read-only failures of old commands."""

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    for name in ("sema", "coge", "repo"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()

    def run(command, *fragments, ok=False):
        result = subprocess.run(command, capture_output=True, text=True)
        assert (result.returncode == 0) == ok, result.stdout + result.stderr
        text = result.stdout + result.stderr
        normalized = " ".join(text.split())
        for fragment in fragments:
            assert fragment in normalized, (fragment, text)
        return result

    sema_help = run([str(args.sema), "--help"], "coge --legacy", "--contracts", ok=True)
    assert "--emit-sema" not in sema_help.stdout.splitlines()[0]
    run([str(args.coge), "--help"], "--emit-sema", "coge --legacy", ok=True)
    run([str(args.coge), "--legacy", "--help"], "Migration:", "*_typed.coge", ok=True)
    with tempfile.TemporaryDirectory(prefix="agsem-migration-messages-") as directory:
        root = Path(directory)
        old = root / "old.sema"
        shutil.copyfile(args.repo / "tests/fixtures/legacy/toyc_full.sema", old)
        before = set(root.iterdir())
        for binary in (args.sema, args.coge):
            run([str(binary), "--emit-rust-dir", str(root / "output"), str(old)],
                "document.syntax", "coge --legacy", "--contracts")
        run([str(args.sema), "--legacy", str(old)], "coge --legacy", "DOCUMENT_CLI.md")
        source = args.repo / "coge/examples/toyc/toyc.coge"
        contract = args.repo / "contracts/toyc-runtime-v1.json"
        run([str(args.sema), "--contracts", str(contract), str(source)],
            "document.wrong_kind", "coge --emit-sema", "exports use sema")
        run([str(args.coge), str(source)], "--contracts FILE", "contracts/")
        run([str(args.coge), str(args.repo / "sema/examples/toyscope_1/toyscope_1.sema")],
            "document.wrong_kind", "exports use sema")
        assert before == set(root.iterdir()), "failed migration commands wrote files"
    run([str(args.coge), "--legacy", str(args.repo / "tests/fixtures/legacy/minimal.sema")],
        "compatibility mode", "*_typed.coge", ok=True)

    checks = ("return_policy", "assignment_policy", "condition_policy", "statement_ir",
              "flow_actions", "selection_policy", "lowering_model", "backend_c",
              "generated_modules", "execution_rejection")
    for check in checks:
        script = args.repo / "tools" / ("check_" + check + ".py")
        language = "toycp" if check == "selection_policy" else "toyc"
        run([sys.executable, str(script), "--sema", str(args.sema),
             "--source", str(args.repo / "coge/examples" / language / (language + ".coge"))],
            "--contracts", "Migration:", "coge/examples/" + language + "/" + language + ".coge",
            "expects the coge binary")
        run([sys.executable, str(script), "--help"], "Migration:", "--sema", ok=True)
    run([sys.executable, str(args.repo / "tools/regenerate_examples.py"), "--help"],
        "compatibility alias", "--sema-cli", "expects the coge binary", ok=True)
    print("CLI and ten migrated checks: migration guidance and legacy compatibility OK")


if __name__ == "__main__":
    main()

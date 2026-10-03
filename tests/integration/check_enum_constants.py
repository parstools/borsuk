"""Check schema-driven enum expansion, inspection, inference and rejection."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    for name in ("sema", "coge", "repo"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()

    def run(binary, *arguments, ok=True, error=None):
        result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, text=True)
        assert (result.returncode == 0) == ok, result.stdout + result.stderr
        if error:
            assert error in result.stderr, result.stderr
        return result

    with tempfile.TemporaryDirectory(prefix="agsem-enums-") as temporary:
        root = Path(temporary)
        contract = root / "contract.json"
        schema = {"format": 1, "id": "enum-pilot", "version": "1", "semantic": [
            {"name": "Operator", "kind": "enum", "variants": {"Add": [], "Data": [{"name": "Int"}]}},
            {"name": "Context", "kind": "opaque"}]}
        contract.write_text(json.dumps(schema))
        source = root / "pilot.sema"
        text = """sema EnumPilot; format 1;
grammar EnumPilot;
semantic_model {
    rust_context Context;
    analyzer program() -> Operator;
}
node program : EOF #Empty analysis { result = Operator.Add; };
IDENT : [a-z]+;
"""
        source.write_text(text)
        output = root / "model.json"
        run(args.sema, "--inspect-model", output, "--contracts", contract, source)
        model = json.loads(output.read_text())
        assert model["format"] == "agsem-checked-semantic-expansion-v3"
        assert model["source_sha256"] == hashlib.sha256(source.read_bytes()).hexdigest()
        constant, = model["enum_constants"]
        assert (constant["type"], constant["variant"], constant["rust"]) == ("Operator", "Add", "crate::Operator::Add")
        assert text[constant["begin_byte"]:constant["end_byte"]] == "Operator.Add"
        before = output.read_bytes()
        run(args.sema, "--inspect-model", output, "--contracts", contract, source, ok=False, error="output exists")
        run(args.sema, "--force", "--inspect-model", output, "--contracts", contract, source)
        assert before == output.read_bytes()
        run(args.sema, "--force", "--inspect-model", source, "--contracts", contract, source, ok=False, error="output.would_overwrite_input")
        run(args.sema, "--force", "--inspect-model", contract, "--contracts", contract, source, ok=False, error="output.would_overwrite_input")
        assert source.read_text() == text
        for variant, error in (("Missing", "field or variant has no contract"), ("Data", "enum variant requires payload")):
            source.write_text(text.replace("Operator.Add", "Operator." + variant))
            target = root / (variant + ".json")
            run(args.sema, "--inspect-model", target, "--contracts", contract, source, ok=False, error=error)
            assert not target.exists()
        # A local identifier cannot be interpreted as a type namespace.
        source.write_text(text.replace("result =", "let Operator = 1; result ="))
        run(args.sema, "--contracts", contract, source, ok=False)
        source.write_text(text.replace("-> Operator", "-> Int"))
        run(args.sema, "--contracts", contract, source, ok=False, error="analyzer result must have type Int")
        # Inherited declarations activate inference; the same enum type must be inferred.
        source.write_text(text.replace("analyzer program() -> Operator;", "inherited seed: Int;"))
        run(args.sema, "--force", "--inspect-model", output, "--contracts", contract, source)
        assert json.loads(output.read_text())["enum_constants"][0]["type"] == "Operator"
        source.write_text(text)
        schema["semantic"][0]["rust"] = ["crate", "model", "Operator"]
        contract.write_text(json.dumps(schema))
        run(args.sema, "--emit-rust-dir", root / "rust", "--contracts", contract, source)
        assert "crate::model::Operator::Add" in (root / "rust/sema_gen.rs").read_text()
        source.write_text(text.replace("rust_context Context;", "rust_context Context;\n    function addition() -> Operator { return Operator.Add; }"))
        run(args.sema, "--force", "--inspect-model", output, "--contracts", contract, source)
        inspected = json.loads(output.read_text())
        assert len(inspected["enum_constants"]) == 2
        assert inspected["functions"][0]["result"] == "Operator"
        schema["semantic"][0]["arity"] = 1
        contract.write_text(json.dumps(schema))
        run(args.sema, "--contracts", contract, source, ok=False, error="generic structural contracts require explicit type parameters")
        for language in ("toyc", "toycp"):
            source = args.repo / "coge/examples" / language / (language + ".coge")
            contract = args.repo / "contracts" / (language + "-runtime-v1.json")
            output = root / (language + ".json")
            run(args.coge, "--inspect-model", output, "--contracts", contract, source)
            model = json.loads(output.read_text())
            constants = model["enum_constants"]
            assert len(constants) == 15
            assert {c["variant"] for c in constants if c["type"] == "Operator"} == {
                "Add", "Subtract", "Multiply", "Divide", "Less", "LessEqual", "Greater", "GreaterEqual", "Equal", "NotEqual"}
            assert {c["variant"] for c in constants if c["type"] == "AssignmentOp"} == {
                "Set", "Add", "Subtract", "Multiply", "Divide"}
            assert not any(f["name"].startswith("op_") or f["name"].startswith("assignment_") for f in model["functions"])
            export = root / (language + ".sema")
            run(args.coge, "--emit-sema", export, "--contracts", contract, source)
            projected = root / (language + "-sema.json")
            run(args.sema, "--inspect-model", projected, "--contracts", contract, export)
            assert [(c["type"], c["variant"], c["rust"]) for c in json.loads(projected.read_text())["enum_constants"]] == [
                (c["type"], c["variant"], c["rust"]) for c in constants]
    print("Typed enum expansion and inspection: OK")


if __name__ == "__main__":
    main()

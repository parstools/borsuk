"""Verify checked collection expansion, ownership, inference and projection."""
import argparse
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

    with tempfile.TemporaryDirectory(prefix="agsem-collections-") as temporary:
        root = Path(temporary)
        contract = root / "contract.json"
        contract.write_text(json.dumps({"format": 1, "id": "collections-pilot", "version": "1", "semantic": [
            {"name": "Context", "kind": "opaque"},
            {"name": "Element", "kind": "opaque"}]}))
        source = root / "pilot.sema"
        text = """sema Collections; format 1;
grammar Collections;
semantic_model {
    rust_context Context;
    analyzer program() -> List<Int>;
    function empty_int() -> List<Int> { return List.empty(Int); }
    function single_int(value: Int) -> List<Int> { return List.single(value); }
    function append_int(values: List<Int>, value: Int) -> List<Int> {
        return List.append(values, value);
    }
    function nested(values: List<List<Int>>, value: List<Int>) -> List<List<Int>> {
        return List.append(values, value);
    }
    function single_list(value: List<Int>) -> List<List<Int>> { return List.single(value); }
    function append_owned(values: List<OwnedText>, value: OwnedText) -> List<OwnedText> {
        return List.append(values, value);
    }
    function append_element(values: List<Element>, value: Element) -> List<Element> {
        return List.append(values, value);
    }
}
node program : EOF #Empty analysis {
    let first = List.single(3);
    result = List.append(first, 7);
};
IDENT : [a-z]+;
"""
        source.write_text(text)
        output = root / "model.json"
        run(args.sema, "--inspect-model", output, "--contracts", contract, source)
        model = json.loads(output.read_text())
        assert model["format"] == "agsem-checked-semantic-expansion-v3"
        operations = model["collection_operations"]
        assert len(operations) == 9, operations
        assert {item["operation"] for item in operations} == {"empty", "single", "append"}
        for item in operations:
            span = text[item["begin_byte"]:item["end_byte"]]
            assert span.startswith("List." + item["operation"]), span
            assert item["result_type"] == "List<" + item["element_type"] + ">"
            assert len(item["operand_types"]) == len(item["operand_ownership"])
            assert item["rust"]
        nested = next(item for item in operations if item["operation"] == "append" and item["element_type"] == "List<Int>")
        assert nested["operand_ownership"] == ["borrow and clone elements", "clone list value"]
        assert nested["rust_element_type"] == "Vec<i64>"
        before = output.read_bytes()
        run(args.sema, "--force", "--inspect-model", output, "--contracts", contract, source)
        assert output.read_bytes() == before

        # Compile the actual emitted function module. Non-Copy elements and
        # borrowed nested list parameters exercise the Rust ownership boundary.
        generated = root / "generated"
        run(args.sema, "--emit-rust-dir", generated, "--contracts", contract, source)
        harness = root / "harness.rs"
        harness.write_text('''#![allow(unused_variables)]
pub struct Context;
#[derive(Clone, Debug, PartialEq)]
pub struct Element(String);
#[path = "generated/sema_lib_gen.rs"] mod sema_lib_gen;
#[test] fn collection_values_preserve_order_and_input() {
    let ctx = Context;
    assert!(sema_lib_gen::empty_int(&ctx).is_empty());
    assert_eq!(sema_lib_gen::single_int(&ctx, 4), vec![4]);
    let original = vec![3, 7];
    assert_eq!(sema_lib_gen::append_int(&ctx, &original, 11), vec![3, 7, 11]);
    assert_eq!(original, vec![3, 7]);
    let nested = vec![vec![1], vec![2, 3]];
    assert_eq!(sema_lib_gen::nested(&ctx, &nested, &original), vec![vec![1], vec![2, 3], vec![3, 7]]);
    assert_eq!(nested, vec![vec![1], vec![2, 3]]);
    assert_eq!(sema_lib_gen::single_list(&ctx, &original), vec![vec![3, 7]]);
    let strings = vec![String::from("a"), String::from("b")];
    assert_eq!(sema_lib_gen::append_owned(&ctx, &strings, String::from("c")), vec!["a", "b", "c"]);
    assert_eq!(strings, vec!["a", "b"]);
    let values = vec![Element(String::from("first"))];
    assert_eq!(sema_lib_gen::append_element(&ctx, &values, Element(String::from("second"))),
               vec![Element(String::from("first")), Element(String::from("second"))]);
    assert_eq!(values, vec![Element(String::from("first"))]);
}
''')
        executable = root / "harness"
        run("rustc", "--edition=2024", "--test", harness, "-o", executable)
        run(executable)

        cases = [
            ("List.single(3)", "List.single()", "wrong argument count for List.single"),
            ("List.single(3)", "List.single(3, 4)", "wrong argument count for List.single"),
            ("List.append(first, 7)", "List.append(first)", "wrong argument count for List.append"),
            ("List.append(first, 7)", "List.append(3, 7)", "List.append requires a List operand"),
            ("List.append(first, 7)", "List.append(first, true)", "element type mismatch for List.append"),
            ("List.single(3)", "List.single(none)", "collection operation requires a value element type"),
            ("rust_context Context;", "rust_context Context; function bad(value: Node<program>) -> List<Node<program>> { return List.single(value); }", "collection operation requires a value element type"),
            ("rust_context Context;", "rust_context Context; function bad(value: Option<Node<program>>) -> List<Option<Node<program>>> { return List.single(value); }", "collection operation requires a value element type"),
            ("List.single(3)", "List.single(value: 3)", "positional operands"),
            ("List.single(3)", "List.unknown(3)", "unknown collection operation"),
            ("List.empty(Int)", "List.empty(Missing)", "unresolved type"),
            ("List.empty(Int)", "List.empty(List)", "wrong type arity"),
            ("List.empty(Int)", "List.empty(1 + 2)", "explicit element type name"),
            ("let first =", "let List = 1; let first =", None),
            ("analyzer program() -> List<Int>", "analyzer program() -> List<Bool>", "analyzer result must have type List<Bool>"),
        ]
        for index, (old, new, error) in enumerate(cases):
            source.write_text(text.replace(old, new))
            target = root / ("invalid-" + str(index) + ".json")
            run(args.sema, "--inspect-model", target, "--contracts", contract, source, ok=False, error=error)
            assert not target.exists()

        # Inference must resolve element types inside List, including a result
        # anchored only by append's second operand after a recursive analysis.
        inferred = text.replace("analyzer program() -> List<Int>;", "inherited seed: Int;")
        source.write_text(inferred)
        run(args.sema, "--force", "--inspect-model", output, "--contracts", contract, source)
        assert json.loads(output.read_text())["analyzers"][0]["result"] == "List<Int>"
        for action in ("List.single(seed)", "List.empty(Int)", "List.append(List.single(seed), seed)"):
            simple = """sema Inferred; format 1; grammar Inferred;
semantic_model { rust_context Context; inherited seed: Int; }
node program : EOF #Empty analysis { result = ACTION; };
IDENT : [a-z]+;
""".replace("ACTION", action)
            source.write_text(simple)
            run(args.sema, "--force", "--inspect-model", output, "--contracts", contract, source)
            assert json.loads(output.read_text())["analyzers"][0]["result"] == "List<Int>"

        recursive = """sema Recursive; format 1; grammar Recursive;
semantic_model { rust_context Context; inherited seed: Int; }
node program : nested_items=items EOF analysis {
    analyze nested_items -> values; result = List.append(values, seed);
};
node items : nested_items=items ID #More analysis {
    analyze nested_items -> values; result = List.append(values, seed);
} | NUMBER #Base analysis { result = List.empty(Int); };
ID : [a-z]+;
NUMBER : [0-9]+;
"""
        source.write_text(recursive)
        run(args.sema, "--force", "--inspect-model", output, "--contracts", contract, source)
        assert {item["result"] for item in json.loads(output.read_text())["analyzers"]} == {"List<Int>"}
        source.write_text(recursive.replace("result = List.append(values, seed);", "result = List.single(values);"))
        run(args.sema, "--contracts", contract, source, ok=False, error="recursive inferred collection type")

        for language, count in (("toyc", 6), ("toycp", 11)):
            source = args.repo / "coge/examples" / language / (language + ".coge")
            contract = args.repo / "contracts" / (language + "-runtime-v1.json")
            output = root / (language + ".json")
            run(args.coge, "--inspect-model", output, "--contracts", contract, source)
            model = json.loads(output.read_text())
            operations = model["collection_operations"]
            assert len(operations) == count
            assert {item["element_type"] for item in operations} == {"ExprId", "ParameterId"}
            factories = {"empty_arguments", "singleton_argument", "append_argument",
                         "empty_parameters", "singleton_parameter", "append_parameter"}
            assert not factories.intersection(item["name"] for item in model["functions"])
            projected = root / (language + ".sema")
            run(args.coge, "--emit-sema", projected, "--contracts", contract, source)
            inspected = root / (language + "-projected.json")
            run(args.sema, "--inspect-model", inspected, "--contracts", contract, projected)
            normalized = lambda items: [{key: value for key, value in item.items()
                                        if key not in ("begin_byte", "end_byte")} for item in items]
            assert normalized(operations) == normalized(json.loads(inspected.read_text())["collection_operations"])


if __name__ == "__main__":
    main()

use super::*;
use crate::analyze_source;

include!("../generated/interpreter_properties_gen.rs");

fn ready(source: &str) -> Context {
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    context
}

#[test]
fn generated_execution_properties_hold_for_toycp() {
    let context = ready("int main() { return 0; }");
    let interpreter = Interpreter::new(&context).unwrap();
    check_execution_properties(&interpreter);
}

#[test]
fn source_analysis_runs_plain_function() {
    let context = ready("int main() { int answer = 5; return answer + 1; }");
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(6));
}

#[test]
fn return_presence_and_conversion_errors_accumulate() {
    let context = analyze_source(
        "void has_value() { return 1; } \
         int lacks_value() { return; } \
         int wrong_type() { bool value = true; return value; } \
         int main() { return 0; }",
    )
    .unwrap();
    let messages: Vec<_> = context
        .diagnostics
        .iter()
        .map(|error| error.message.as_str())
        .collect();
    assert_eq!(
        messages.iter().filter(|message| **message == "invalid return value").count(),
        2
    );
    assert!(messages.contains(&"incompatible return type"));
}

#[test]
fn source_analysis_runs_constructor_method_and_return_cleanup() {
    let context = ready(
        "int counter; \
         class Widget { public: Widget(int n) { value = n; } \
         ~Widget() { counter += value; } \
         int increment() { value++; return value; } \
         private: int value; }; \
         int main() { Widget item(5); int result = item.increment(); return result; }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(6));
    assert_eq!(interpreter.global(SymbolId(0)), Some(&Value::Int(6)));
}

#[test]
fn source_analysis_constructs_base_before_derived_and_destroys_reverse() {
    let context = ready(
        "int log; \
         class Base { public: Base() { log = log * 10 + 1; } \
         ~Base() { log = log * 10 + 4; } }; \
         class Child : Base { public: Child() { log = log * 10 + 2; } \
         ~Child() { log = log * 10 + 3; } }; \
         int main() { Child child(); return 0; }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(0));
    assert_eq!(interpreter.global(SymbolId(0)), Some(&Value::Int(1234)));
}

#[test]
fn out_of_class_definitions_execute_against_declared_members() {
    let context = ready(
        "int log; \
         class Counter { public: Counter(int n); ~Counter(); int increment(); \
         private: int value; }; \
         Counter::Counter(int n) { value = n; } \
         Counter::~Counter() { log += value; } \
         int Counter::increment() { value++; return value; } \
         int main() { Counter counter(7); return counter.increment(); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(8));
    assert_eq!(interpreter.global(SymbolId(0)), Some(&Value::Int(8)));
}

#[test]
fn default_construction_runs_for_global_and_local_objects() {
    let context = ready(
        "int log; \
         class Item { public: Item() { log = log * 10 + 1; } \
         ~Item() { log = log * 10 + 2; } }; \
         Item global; \
         int main() { Item local; return log; }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.global(SymbolId(0)), Some(&Value::Int(1)));
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(11));
    assert_eq!(interpreter.global(SymbolId(0)), Some(&Value::Int(1122)));
}

#[test]
fn inaccessible_field_reports_semantic_error_and_blocks_execution() {
    let context = analyze_source(
        "class Box { private: int value; public: Box() { value = 3; } }; \
         int main() { Box box(); return box.value; }",
    )
    .unwrap();
    assert!(
        context
            .diagnostics
            .iter()
            .any(|error| error.message == "member is not accessible"),
        "{:?}",
        context.diagnostics
    );
    assert_eq!(
        Interpreter::new(&context).err().unwrap().message,
        "semantic errors prevent execution"
    );
}

#[test]
fn source_analysis_evaluates_dynamic_array_index() {
    let context =
        ready("int main() { int values[3]; int i = 1; values[i] = 7; return values[i]; }");
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(7));
}

#[test]
fn compound_assignment_evaluates_index_once() {
    let context = ready(
        "int calls; \
         class Box { public: int values[1]; Box() { values[0] = 4; } }; \
         int nextIndex() { calls += 1; return 0; } \
         int main() { Box box(); box.values[nextIndex()] += 3; \
         return calls * 10 + box.values[0]; }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(17));
}

#[test]
fn compound_assignment_binds_target_before_right_hand_side() {
    let context = ready(
        "int calls; \
         class Box { public: int values[1]; Box() { values[0] = 4; } }; \
         int nextIndex() { calls += 1; return 0; } \
         int main() { Box box(); box.values[nextIndex()] += nextIndex(); \
         return calls * 10 + box.values[0]; }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(24));
}

#[test]
fn dynamic_array_index_reports_runtime_bounds_error() {
    let context = ready("int main() { int values[2]; int i = 3; values[i] = 7; return 0; }");
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter.run_main().unwrap_err().message,
        "array index out of bounds"
    );
}

#[test]
fn class_array_constructs_forward_and_destroys_reverse() {
    let context = ready(
        "int log; class Item { public: Item() { log = log * 10 + 1; } \
         ~Item() { log = log * 10 + 2; } }; \
         int main() { Item items[2]; return log; }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(11));
    assert_eq!(interpreter.global(SymbolId(0)), Some(&Value::Int(1122)));
}

#[test]
fn derived_method_can_read_protected_base_field() {
    let context = ready(
        "class Base { protected: int value; public: Base() { value = 4; } }; \
         class Child : Base { public: int get() { return value; } }; \
         int main() { Child child(); return child.get(); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(4));
}

#[test]
fn external_access_to_protected_base_field_is_rejected() {
    let context = analyze_source(
        "class Base { protected: int value; }; \
         class Child : Base { }; int main() { Child child(); return child.value; }",
    )
    .unwrap();
    assert!(
        context
            .diagnostics
            .iter()
            .any(|error| error.message == "member is not accessible")
    );
}

#[test]
fn inherited_method_uses_base_subobject_as_self() {
    let context = ready(
        "class Base { public: int value; Base() { value = 3; } \
         int get() { return value; } }; \
         class Child : Base { public: int increment() { value++; return get(); } }; \
         int main() { Child child(); return child.increment(); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(4));
}

#[test]
fn constructor_overloads_select_matching_signature() {
    let context = ready(
        "class Item { public: Item() { value = 1; } \
         Item(int n) { value = n; } int get() { return value; } \
         private: int value; }; \
         int main() { Item first(); Item second(7); return first.get() * 10 + second.get(); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(17));
}

#[test]
fn constructor_call_applies_selected_argument_conversion() {
    let context = ready(
        "class Item { public: Item(float n) { value = 9; } \
         int get() { return value; } private: int value; }; \
         int main() { Item item(7); return item.get(); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(9));
}

#[test]
fn constructor_selection_reports_tie_and_inaccessible_best_match() {
    let ambiguous = analyze_source(
        "class Item { public: Item(int x, float y) {} Item(float x, int y) {} }; \
         int main() { Item item(1, 1); return 0; }",
    )
    .unwrap();
    assert!(ambiguous
        .diagnostics
        .iter()
        .any(|error| error.message == "ambiguous constructor call"));

    let inaccessible = analyze_source(
        "class Item { private: Item(int x) {} public: Item(float x) {} }; \
         int main() { Item item(1); return 0; }",
    )
    .unwrap();
    assert!(inaccessible
        .diagnostics
        .iter()
        .any(|error| error.message == "constructor is not accessible"));
}

#[test]
fn out_of_class_constructor_overload_matches_its_declaration() {
    let context = ready(
        "class Item { public: Item(); Item(int n); int get(); private: int value; }; \
         Item::Item() { value = 2; } Item::Item(int n) { value = n; } \
         int Item::get() { return value; } \
         int main() { Item item(4); return item.get(); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(4));
}

#[test]
fn missing_default_constructor_is_semantic_error() {
    let context = analyze_source(
        "class Item { public: Item(int n) { value = n; } private: int value; }; \
         int main() { Item item; return 0; }",
    )
    .unwrap();
    assert!(
        context
            .diagnostics
            .iter()
            .any(|error| error.message == "default constructor not declared")
    );
}

#[test]
fn syntax_recovery_preserves_following_semantic_errors() {
    let context =
        analyze_source("int main() { int first = 1 int second = missing; return first; }").unwrap();
    let messages: Vec<_> = context
        .diagnostics
        .iter()
        .map(|error| error.message.as_str())
        .collect();
    assert!(
        messages
            .iter()
            .any(|message| message.starts_with("syntax error: inserted")),
        "{messages:?}"
    );
    assert!(
        messages
            .iter()
            .any(|message| *message == "variable not declared"),
        "{messages:?}"
    );
}

#[test]
fn nested_block_destroys_its_objects_before_outer_return() {
    let context = ready(
        "int log; class Item { public: Item() { log = log * 10 + 1; } \
         ~Item() { log = log * 10 + 2; } }; \
         int main() { Item outer; { Item inner; } return log; }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(interpreter.run_main().unwrap(), Value::Int(112));
    assert_eq!(interpreter.global(SymbolId(0)), Some(&Value::Int(1122)));
}

use super::*;
use crate::analyze_source;

include!("../generated/interpreter_properties_gen.rs");

#[test]
fn generated_execution_properties() {
    let context = ready("int main() { return 0; }");
    let interpreter = Interpreter::new(&context).unwrap();
    check_execution_properties(&interpreter);
}

fn ready(source: &str) -> Context {
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    context
}

#[test]
fn loops_calls_and_recursive_frames_execute_from_ir() {
    let context = ready(
        "int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); } \
         int main() { int total = 0; for (int i = 0; i < 5; i++) total += i; \
         while (total < 12) total++; return total + fib(6); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter.call_named("main", vec![]).unwrap(),
        Value::Int(20)
    );
    assert_eq!(
        interpreter.call_named("fib", vec![Value::Int(5)]).unwrap(),
        Value::Int(5)
    );
    assert_eq!(
        interpreter
            .call_named("fib", vec![Value::Bool(true)])
            .unwrap_err()
            .message,
        "incompatible argument type"
    );
}

#[test]
fn globals_fields_and_function_arguments_keep_values() {
    let context = ready(
        "struct Pair { int left; int right; }; Pair pair; int counter; \
         int add(int n) { counter += n; pair.left = counter; return pair.left + pair.right; } \
         int main() { return add(3) + add(4); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter.call_named("main", vec![]).unwrap(),
        Value::Int(10)
    );
    assert_eq!(interpreter.global("counter"), Some(&Value::Int(7)));
    assert_eq!(
        interpreter.global("pair"),
        Some(&Value::Struct(vec![Value::Int(7), Value::Int(0)]))
    );
}

#[test]
fn local_struct_fields_are_initialized_independently() {
    let context = ready(
        "struct Pair { int left; int right; }; \
         int main() { Pair pair; pair.left = 2; pair.right = 3; return pair.left + pair.right; }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter.call_named("main", vec![]).unwrap(),
        Value::Int(5)
    );
}

#[test]
fn failed_nested_calls_release_frames_and_preserve_error_source() {
    let context = ready(
        "int divide(int n) { return 12 / n; } \
         int outer(int n) { int local = n; return divide(local); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    let direct = interpreter
        .call_named("divide", vec![Value::Int(0)])
        .unwrap_err();
    let nested = interpreter
        .call_named("outer", vec![Value::Int(0)])
        .unwrap_err();
    assert_eq!(nested, direct);
    assert_eq!(interpreter.runtime.store.depth(), 0);
    assert_eq!(
        interpreter
            .call_named("outer", vec![Value::Int(3)])
            .unwrap(),
        Value::Int(4)
    );
    assert_eq!(interpreter.runtime.store.depth(), 0);
    interpreter.set_step_limit(0);
    assert_eq!(
        interpreter
            .call_named("outer", vec![Value::Int(3)])
            .unwrap_err()
            .message,
        "execution limit exceeded"
    );
    assert_eq!(interpreter.runtime.store.depth(), 0);
    interpreter.set_step_limit(100);
    assert_eq!(
        interpreter
            .call_named("outer", vec![Value::Int(6)])
            .unwrap(),
        Value::Int(2)
    );
}

#[test]
fn call_arguments_are_evaluated_once_from_left_to_right() {
    let context = ready(
        "int counter; int next() { counter++; return counter; } \
         int pair(int a, int b) { return a * 10 + b; } \
         int main() { return pair(next(), next()); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter.call_named("main", vec![]).unwrap(),
        Value::Int(12)
    );
    assert_eq!(interpreter.global("counter"), Some(&Value::Int(2)));
    assert_eq!(
        interpreter.call_named("pair", vec![]).unwrap_err().message,
        "wrong number of arguments"
    );
    assert_eq!(interpreter.runtime.store.depth(), 0);
}

#[test]
fn aggregate_initialization_and_argument_copying_are_recursive() {
    let context = ready(
        "struct Pair { int left; int right; }; Pair pair; Pair pairs[2]; \
         int sum(Pair value) { value.left++; return value.left + value.right; } \
         int main() { pair.left = 2; pair.right = 3; return sum(pair); }",
    );
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter.global("pairs"),
        Some(&Value::Array(vec![
            Value::Struct(vec![Value::Int(0), Value::Int(0)]),
            Value::Struct(vec![Value::Int(0), Value::Int(0)]),
        ]))
    );
    assert_eq!(
        interpreter.call_named("main", vec![]).unwrap(),
        Value::Int(6)
    );
    assert_eq!(
        interpreter.global("pair"),
        Some(&Value::Struct(vec![Value::Int(2), Value::Int(3)]))
    );
    let bad = Value::Struct(vec![Value::Int(1), Value::Bool(false)]);
    assert_eq!(
        interpreter
            .call_named("sum", vec![bad])
            .unwrap_err()
            .message,
        "incompatible argument type"
    );
    let ty = Type::Array(Box::new(Type::Array(Box::new(Type::Int), 2)), 1);
    let local = interpreter.value_for_type(ty.clone(), false).unwrap();
    assert_eq!(
        local,
        Value::Array(vec![Value::Array(vec![Value::Uninitialized; 2])])
    );
    assert!(!interpreter.value_matches_type(local, ty.clone()).unwrap());
    let global = interpreter.value_for_type(ty.clone(), true).unwrap();
    assert!(interpreter.value_matches_type(global, ty).unwrap());
}

#[test]
fn runtime_checks_and_limits_report_source_errors() {
    let context = ready("int divide(int n) { return 10 / n; }");
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter
            .call_named("divide", vec![Value::Int(2)])
            .unwrap(),
        Value::Int(5)
    );
    let error = interpreter
        .call_named("divide", vec![Value::Int(0)])
        .unwrap_err();
    assert_eq!(error.message, "division by zero");
    assert!(error.source.end_byte > error.source.begin_byte);

    let context = ready("int main() { return 2147483647 + 1; }");
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter.call_named("main", vec![]).unwrap_err().message,
        "integer overflow"
    );

    let context = ready("void main() { while (true) {} }");
    let mut interpreter = Interpreter::new(&context).unwrap();
    interpreter.set_step_limit(100);
    assert_eq!(
        interpreter.call_named("main", vec![]).unwrap_err().message,
        "execution limit exceeded"
    );
}

#[test]
fn undefined_functions_and_invalid_semantics_cannot_execute() {
    let context = ready("int external(); int main() { return external(); }");
    let mut interpreter = Interpreter::new(&context).unwrap();
    assert_eq!(
        interpreter.call_named("main", vec![]).unwrap_err().message,
        "undefined function"
    );

    let context = analyze_source("int main() { int x = x; return 0; }").unwrap();
    assert_eq!(
        Interpreter::new(&context).err().unwrap().message,
        "semantic errors prevent execution"
    );
}

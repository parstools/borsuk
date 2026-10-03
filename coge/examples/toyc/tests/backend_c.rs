use borsuk_toyc::{FunctionId, Interpreter, Value, analyze_source, backend_c_gen};
use std::fs;
use std::io::Write;
use std::process::{Command, Stdio};
use std::time::{SystemTime, UNIX_EPOCH};

fn compile_and_run(source: &str) -> std::process::Output {
    let suffix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let binary =
        std::env::temp_dir().join(format!("agsem-c-backend-{}-{suffix}", std::process::id()));
    let mut compiler = Command::new("cc")
        .args([
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-x",
            "c",
            "-o",
        ])
        .arg(&binary)
        .arg("-")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    compiler
        .stdin
        .take()
        .unwrap()
        .write_all(source.as_bytes())
        .unwrap();
    let compilation = compiler.wait_with_output().unwrap();
    assert!(
        compilation.status.success(),
        "{}\n{}",
        source,
        String::from_utf8_lossy(&compilation.stderr)
    );
    let result = Command::new(&binary).output().unwrap();
    fs::remove_file(binary).unwrap();
    result
}

#[test]
fn generated_c_backend_compiles_and_preserves_comments() {
    let context = analyze_source("int main() { /* before */ return 2 + 3; // after\n }").unwrap();
    assert!(context.diagnostics.is_empty());
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert!(c.contains("int main(void)"));
    assert!(c.contains("agsem_checked_add_i32"));
    assert!(!c.contains("goto"));
    assert!(c.find("/* before */").unwrap() < c.find("return (int)").unwrap());
    assert!(c.find("return (int)").unwrap() < c.find("// after").unwrap());
    assert_eq!(compile_and_run(&c).status.code(), Some(5));
}

#[test]
fn generated_c_backend_runs_local_initialization_load_and_assignment() {
    let context =
        analyze_source("int main() { int x = 2; int y = x + 3; x = y + 1; return x; }").unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert!(c.contains("int32_t agsem_slot0;"));
    assert!(c.contains("int32_t agsem_slot1;"));
    assert_eq!(compile_and_run(&c).status.code(), Some(6));
}

#[test]
fn generated_c_backend_runs_assignment_after_declaration() {
    let context = analyze_source("int main() { int x; x = 4; return x + 1; }").unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(5));
}

#[test]
fn generated_c_backend_preserves_nested_scope_shadowing() {
    let source = "int main() { int x = 2; { int x = 5; x = x + 1; } return x; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert!(c.contains("int32_t agsem_slot0;"));
    assert!(c.contains("int32_t agsem_slot1;"));
    assert_eq!(compile_and_run(&c).status.code(), Some(2));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(2)
    );
}

#[test]
fn generated_c_backend_runs_both_if_branches() {
    for (initial, expected) in [(2, 5), (3, 9)] {
        let source = format!(
            "int main() {{ int x = {initial}; if (x == 2) {{ x = 5; }} else {{ x = 9; }} return x; }}"
        );
        let context = analyze_source(&source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        assert!(c.contains("if (agsem_v"));
        assert!(c.contains("} else {"));
        assert!(!c.contains("goto"));
        assert_eq!(compile_and_run(&c).status.code(), Some(expected));
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap(),
            Value::Int(expected)
        );
    }
}

#[test]
fn generated_c_backend_runs_if_without_else() {
    let source = "int main() { int x = 4; if (x < 3) x = 9; return x; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(4));
}

#[test]
fn generated_c_backend_handles_return_from_each_if_branch() {
    let source =
        "int main() { int x = 2; if (x <= 2) { /* yes */ return 7; } else { /* no */ return 9; } }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert!(c.find("/* yes */").unwrap() < c.find("/* no */").unwrap());
    assert_eq!(compile_and_run(&c).status.code(), Some(7));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(7)
    );
}

#[test]
fn generated_c_backend_accepts_boolean_condition() {
    let context = analyze_source("int main() { if (true) return 3; else return 7; }").unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(3));
}

#[test]
fn generated_c_backend_rechecks_while_condition() {
    let source = "int main() { int x = 0; while (x < 3) { /* step */ x = x + 1; } return x; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert!(c.contains("while (1) {"));
    assert!(c.contains("if (!agsem_v"));
    assert!(c.contains("/* step */"));
    assert!(!c.contains("goto"));
    assert_eq!(compile_and_run(&c).status.code(), Some(3));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(3)
    );
}

#[test]
fn generated_c_backend_skips_false_while_body() {
    let source = "int main() { int x = 5; while (x < 3) x = 9; return x; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(5));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(5)
    );
}

#[test]
fn generated_c_backend_runs_nested_while_loops() {
    let source = "int main() { int i = 0; int count = 0; while (i < 2) { int j = 0; while (j < 3) { count = count + 1; j = j + 1; } i = i + 1; } return count; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert_eq!(c.matches("while (1) {").count(), 2);
    assert_eq!(compile_and_run(&c).status.code(), Some(6));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(6)
    );
}

#[test]
fn generated_c_backend_runs_for_initializer_condition_and_update() {
    let source =
        "int main() { int sum = 0; for (int i = 0; i < 3; i++) sum = sum + i; return sum; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert!(c.contains("while (1) {"));
    assert!(!c.contains("goto"));
    assert_eq!(compile_and_run(&c).status.code(), Some(3));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(3)
    );
}

#[test]
fn generated_c_backend_skips_false_for_body_and_update() {
    let source = "int main() { int x = 5; for (int i = 4; i < 3; i = i + 1) x = 9; return x; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(5));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(5)
    );
}

#[test]
fn generated_c_backend_skips_for_update_after_return() {
    let source = "int main() { for (int i = 0; i < 3; i = i + 1) { return i + 7; } return 9; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(7));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(7)
    );
}

#[test]
fn generated_c_backend_checks_integer_overflow() {
    let context = analyze_source("int main() { return 2147483647 + 1; }").unwrap();
    assert!(context.diagnostics.is_empty());
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    let result = compile_and_run(&c);
    assert!(!result.status.success());
    assert!(String::from_utf8_lossy(&result.stderr).contains("integer overflow"));
}

#[test]
fn generated_c_backend_runs_checked_integer_arithmetic() {
    for (source, expected) in [
        ("int main() { return (9 - 3) * 2 / 3; }", 4),
        ("int main() { return -(-5); }", 5),
        ("int main() { return 7 / -2; }", 253),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        assert_eq!(compile_and_run(&c).status.code(), Some(expected));
    }
}

#[test]
fn generated_c_backend_reports_arithmetic_errors_like_interpreter() {
    for (source, expected) in [
        (
            "int main() { return 0 - (-2147483647 - 1); }",
            "integer overflow",
        ),
        ("int main() { return 50000 * 50000; }", "integer overflow"),
        ("int main() { return 8 / 0; }", "division by zero"),
        (
            "int main() { return (-2147483647 - 1) / -1; }",
            "integer overflow",
        ),
        (
            "int main() { return -(-2147483647 - 1); }",
            "integer overflow",
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let output = compile_and_run(&c);
        assert!(!output.status.success());
        assert_eq!(String::from_utf8_lossy(&output.stderr).trim(), expected);
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap_err()
                .message,
            expected
        );
    }
}

#[test]
fn generated_c_backend_rejects_call_without_definition() {
    let context = analyze_source("int helper(); int main() { return helper(); }").unwrap();
    assert!(context.diagnostics.is_empty());
    assert!(matches!(
        backend_c_gen::emit_c(&context, FunctionId(0)),
        Err(backend_c_gen::BackendCError::Lowering(
            "function has no body"
        ))
    ));
}

#[test]
fn generated_c_backend_runs_function_calls_with_parameters() {
    let source = "int add(int a, int b) { return a + b; } int main() { return add(2, 3); }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(1)).unwrap();
    assert!(c.contains("static int32_t agsem_f1(int32_t, int32_t);"));
    assert!(c.contains("agsem_f1(agsem_v0, agsem_v1)"));
    assert_eq!(compile_and_run(&c).status.code(), Some(5));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(5)
    );
}

#[test]
fn generated_c_backend_runs_nested_calls_to_later_definition() {
    let source = "int add(int a, int b); int twice(int x); int main() { return add(twice(2), twice(3)); } int twice(int x) { return x + x; } int add(int a, int b) { return a + b; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(2)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(10));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(10)
    );
}

#[test]
fn generated_c_backend_runs_recursive_call() {
    let source = "int fact(int n) { if (n < 2) return 1; return n * fact(n - 1); } int main() { return fact(5); }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(1)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(120));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(120)
    );
}

#[test]
fn generated_c_backend_evaluates_call_arguments_left_to_right() {
    let source =
        "int first(int a, int b) { return a; } int main() { return first(1 / 0, 2147483647 + 1); }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(1)).unwrap();
    let output = compile_and_run(&c);
    assert!(!output.status.success());
    assert_eq!(
        String::from_utf8_lossy(&output.stderr).trim(),
        "division by zero"
    );
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap_err()
            .message,
        "division by zero"
    );
}

#[test]
fn generated_c_backend_converts_char_to_int_and_float() {
    for (source, expected) in [
        ("int main() { char c = 'A'; int n = c + 1; return n; }", 66),
        ("int main() { if ('A' < 66.0) return 7; return 0; }", 7),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        assert_eq!(compile_and_run(&c).status.code(), Some(expected));
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap(),
            Value::Int(expected)
        );
    }
}

#[test]
fn generated_c_backend_converts_int_to_float_and_calls_float_function() {
    let source = "float twice(float x) { return x + x; } int main() { float x = 2; if (twice(x) == 4.0) return 9; return 0; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(1)).unwrap();
    assert!(c.contains("static float agsem_f1(float);"));
    assert_eq!(compile_and_run(&c).status.code(), Some(9));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(9)
    );
}

#[test]
fn generated_c_backend_rounds_int_to_float_like_interpreter() {
    let source = "int main() { if (16777217 == 16777216.0) return 1; return 0; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    assert_eq!(compile_and_run(&c).status.code(), Some(1));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(1)
    );
}

#[test]
fn generated_c_backend_runs_indexed_array_access() {
    for (source, expected) in [
        (
            "int main() { int a[3]; a[0] = 2; a[1] = a[0] + 3; return a[1]; }",
            5,
        ),
        ("int main() { char a[2]; a[0] = 'A'; return a[0]; }", 65),
        (
            "int main() { float a[2]; a[0] = 1.5; if (a[0] == 1.5) return 7; return 0; }",
            7,
        ),
        (
            "int main() { bool a[2]; a[0] = true; if (a[0]) return 8; return 0; }",
            8,
        ),
        (
            "int main() { int a[3]; for (int i = 0; i < 3; i = i + 1) a[i] = i + 2; return a[2] + a[0]; }",
            6,
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        assert_eq!(compile_and_run(&c).status.code(), Some(expected));
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap(),
            Value::Int(expected)
        );
    }
}

#[test]
fn generated_c_backend_runs_indexed_updates() {
    for (source, expected) in [
        (
            "int main() { int a[1]; a[0] = 2; a[0] += 3; a[0] *= 2; a[0]--; a[0]++; a[0] /= 2; return a[0]; }",
            5,
        ),
        (
            "int main() { char a[1]; a[0] = 'A'; a[0] += 2; return a[0]; }",
            67,
        ),
        (
            "int main() { int a[1]; a[0] = 5; a[0] += 0.5; return a[0]; }",
            5,
        ),
        (
            "int main() { float a[1]; a[0] = 1.5; a[0] += 2; if (a[0] == 3.5) return 9; return 0; }",
            9,
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        assert_eq!(compile_and_run(&c).status.code(), Some(expected));
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap(),
            Value::Int(expected)
        );
    }
}

#[test]
fn generated_c_backend_checks_indexed_update_before_rhs() {
    for (source, message) in [
        (
            "int main() { int a[1]; a[0] += 1 / 0; return 0; }",
            "variable used before initialization",
        ),
        (
            "int main() { int a[1]; a[2] += 1 / 0; return 0; }",
            "array index out of bounds",
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let output = compile_and_run(&c);
        assert!(!output.status.success());
        assert_eq!(String::from_utf8_lossy(&output.stderr).trim(), message);
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap_err()
                .message,
            message
        );
    }
}

#[test]
fn generated_c_backend_evaluates_index_once_for_update() {
    let source = "int index() { return 0; } int main() { int a[1]; a[0] = 3; a[index()] += 4; return a[0]; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(1)).unwrap();
    assert_eq!(c.matches("= agsem_f1(").count(), 1);
    assert_eq!(compile_and_run(&c).status.code(), Some(7));
}

#[test]
fn generated_c_backend_shares_zero_initialized_global_array() {
    let source = "int a[2]; int change() { a[1] += 5; return a[1]; } int main() { return a[0] + change() + a[1]; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(1)).unwrap();
    assert!(c.contains("static int32_t agsem_global0[2];"));
    assert_eq!(compile_and_run(&c).status.code(), Some(10));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(10)
    );
}

#[test]
fn generated_c_backend_handles_global_scalar_and_array_bounds() {
    for (source, expected, error) in [
        ("int x; int main() { x = 7; return x; }", 7, None),
        (
            "int a[2]; int main() { a[2] = 1; return 0; }",
            1,
            Some("array index out of bounds"),
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let output = compile_and_run(&c);
        assert_eq!(output.status.code(), Some(expected));
        if let Some(error) = error {
            assert_eq!(String::from_utf8_lossy(&output.stderr).trim(), error);
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap_err()
                    .message,
                error
            );
        } else {
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap(),
                Value::Int(expected)
            );
        }
    }
}

#[test]
fn generated_c_backend_checks_array_bounds_and_initialization() {
    for (source, message) in [
        (
            "int main() { int a[2]; return a[2]; }",
            "array index out of bounds",
        ),
        (
            "int main() { int a[2]; a[-1] = 4; return 0; }",
            "array index out of bounds",
        ),
        (
            "int main() { int a[2]; a[2] = 1 / 0; return 0; }",
            "division by zero",
        ),
        (
            "int main() { int a[2]; return a[1]; }",
            "variable used before initialization",
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let output = compile_and_run(&c);
        assert!(!output.status.success());
        assert_eq!(String::from_utf8_lossy(&output.stderr).trim(), message);
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap_err()
                .message,
            message
        );
    }
}

#[test]
fn generated_c_backend_resets_local_array_on_each_loop_entry() {
    let source = "int main() { for (int i = 0; i < 2; i = i + 1) { int a[1]; if (i == 0) a[0] = 4; else return a[0]; } return 0; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    let output = compile_and_run(&c);
    assert!(!output.status.success());
    assert_eq!(
        String::from_utf8_lossy(&output.stderr).trim(),
        "variable used before initialization"
    );
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap_err()
            .message,
        "variable used before initialization"
    );
}

#[test]
fn generated_c_backend_supports_bool_result_and_local_slot() {
    let source = "bool positive(int x) { return x > 0; } int main() { bool yes = positive(3); if (yes) return 6; return 0; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let c = backend_c_gen::emit_c(&context, FunctionId(1)).unwrap();
    assert!(c.contains("static bool agsem_f1(int32_t);"));
    assert_eq!(compile_and_run(&c).status.code(), Some(6));
    assert_eq!(
        Interpreter::new(&context)
            .unwrap()
            .call_named("main", vec![])
            .unwrap(),
        Value::Int(6)
    );
}

#[test]
fn generated_c_backend_preserves_float_negation_and_signed_zero() {
    for (source, expected) in [
        (
            "int main() { if (-1.5 / 2.0 == -0.75) return 3; return 0; }",
            3,
        ),
        (
            "int main() { if (1.0 / -0.0 < 0.0) return 4; return 0; }",
            4,
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        assert_eq!(compile_and_run(&c).status.code(), Some(expected));
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap(),
            Value::Int(expected)
        );
    }
}

#[test]
fn generated_c_backend_converts_scalar_conditions_to_bool() {
    for (source, expected) in [
        ("int main() { if (2) return 5; return 0; }", 5),
        ("int main() { if (0.0) return 0; return 6; }", 6),
        ("int main() { if ('A') return 7; return 0; }", 7),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        assert_eq!(compile_and_run(&c).status.code(), Some(expected));
        assert_eq!(
            Interpreter::new(&context)
                .unwrap()
                .call_named("main", vec![])
                .unwrap(),
            Value::Int(expected)
        );
    }
}

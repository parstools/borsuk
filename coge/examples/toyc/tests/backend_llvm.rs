use agsem_runtime::llvm_backend::TargetSpec;
use borsuk_toyc::{
    FunctionId, Interpreter, Value, analyze_source, backend_c_gen, backend_llvm_gen,
};
use std::fs;
use std::io::Write;
use std::process::{Command, Output, Stdio};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(0);

fn temporary_suffix() -> String {
    let time = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let sequence = NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed);
    format!("{}-{time}-{sequence}", std::process::id())
}

fn host_target() -> Option<(String, String)> {
    let output = Command::new("clang")
        .args(["-S", "-emit-llvm", "-x", "c", "-", "-o", "-"])
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    let text = String::from_utf8(output.stdout).ok()?;
    let field = |name: &str| {
        text.lines()
            .find_map(|line| line.strip_prefix(name)?.strip_suffix('"'))
            .map(str::to_owned)
    };
    Some((
        field("target triple = \"")?,
        field("target datalayout = \"")?,
    ))
}

fn run_llvm(llvm: &str) -> Output {
    let path = std::env::temp_dir().join(format!("agsem-llvm-{}.ll", temporary_suffix()));
    let bitcode = path.with_extension("bc");
    fs::write(&path, llvm).unwrap();
    let assembly = Command::new("llvm-as")
        .arg(&path)
        .arg("-o")
        .arg(&bitcode)
        .output()
        .unwrap();
    assert!(
        assembly.status.success(),
        "{llvm}\n{}",
        String::from_utf8_lossy(&assembly.stderr)
    );
    let verification = Command::new("opt")
        .arg("-passes=verify")
        .arg("-disable-output")
        .arg(&bitcode)
        .output()
        .unwrap();
    assert!(
        verification.status.success(),
        "{llvm}\n{}",
        String::from_utf8_lossy(&verification.stderr)
    );
    let execution = Command::new("lli").arg(&bitcode).output().unwrap();
    fs::remove_file(path).unwrap();
    fs::remove_file(bitcode).unwrap();
    execution
}

fn run_c(source: &str) -> Output {
    let binary = std::env::temp_dir().join(format!("agsem-llvm-c-{}", temporary_suffix()));
    let mut compiler = Command::new("cc")
        .args(["-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-o"])
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
        "{}",
        String::from_utf8_lossy(&compilation.stderr)
    );
    let execution = Command::new(&binary).output().unwrap();
    fs::remove_file(binary).unwrap();
    execution
}

#[test]
fn llvm_backend_executes_scalar_local_program() {
    let context = analyze_source("int main() { int x; x = 5; return x; }").unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    let llvm = backend_llvm_gen::emit_llvm(
        &context,
        FunctionId(0),
        TargetSpec {
            triple: &triple,
            data_layout: &data_layout,
        },
    )
    .unwrap();
    assert!(llvm.contains("%slot0 = alloca i32"));
    assert!(llvm.contains("store i32 5, ptr %slot0"));
    assert!(llvm.contains("ret i32 %b0_v"));

    if Command::new("llvm-as").arg("--version").output().is_err()
        || Command::new("lli").arg("--version").output().is_err()
        || Command::new("opt").arg("--version").output().is_err()
    {
        eprintln!("LLVM execution skipped: llvm-as, opt or lli unavailable");
        return;
    }
    let execution = run_llvm(&llvm);
    assert_eq!(execution.status.code(), Some(5));
}

#[test]
fn llvm_checked_arithmetic_matches_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || Command::new("llvm-as").arg("--version").output().is_err()
        || Command::new("lli").arg("--version").output().is_err()
        || Command::new("opt").arg("--version").output().is_err()
    {
        eprintln!("LLVM arithmetic execution skipped: unsupported host tools or target");
        return;
    }
    for (source, expected, error) in [
        ("int main() { return 20 + 3 - 4 * 2; }", Some(15), None),
        ("int main() { return 17 / 3; }", Some(5), None),
        (
            "int main() { return 2147483647 + 1; }",
            None,
            Some("integer overflow"),
        ),
        (
            "int main() { return (-2147483647 - 1) - 1; }",
            None,
            Some("integer overflow"),
        ),
        (
            "int main() { return 50000 * 50000; }",
            None,
            Some("integer overflow"),
        ),
        (
            "int main() { return 1 / 0; }",
            None,
            Some("division by zero"),
        ),
        (
            "int main() { return (-2147483647 - 1) / -1; }",
            None,
            Some("integer overflow"),
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(0),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
        if let Some(expected) = expected {
            assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap(),
                Value::Int(expected)
            );
        } else {
            let error = error.unwrap();
            assert_eq!(
                String::from_utf8_lossy(&llvm_result.stderr).trim(),
                error,
                "{source}"
            );
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap_err()
                    .message,
                error
            );
        }
    }
}

#[test]
fn llvm_control_flow_matches_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        eprintln!("LLVM control-flow execution skipped: unsupported host tools or target");
        return;
    }
    for (source, expected) in [
        (
            "int main() { int x = 1; if (x < 2) { x = 7; } else { x = 9; } return x; }",
            7,
        ),
        (
            "int main() { int x = 3; if (x < 2) { x = 7; } else { x = 9; } return x; }",
            9,
        ),
        ("int main() { int x = 0; if (x) x = 8; return x; }", 0),
        ("int main() { int x = 0; if (true) x = 8; return x; }", 8),
        (
            "int main() { int i = 0; while (i < 4) { i = i + 1; } return i; }",
            4,
        ),
        (
            "int main() { int i = 0; while (i < 0) i = i + 1; return i; }",
            0,
        ),
        (
            "int main() { int sum = 0; for (int i = 0; i < 4; i = i + 1) sum = sum + i; return sum; }",
            6,
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(0),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
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
fn llvm_global_scalars_match_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    if ["llvm-as", "lli", "opt"]
        .iter()
        .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        eprintln!("LLVM global execution skipped: unavailable host tools");
        return;
    }
    for (source, expected) in [
        ("int x; int main() { return x; }", 0),
        ("int x; int main() { x = 7; return x; }", 7),
        (
            "int x; int y; int main() { x = 3; y = x + 4; return y; }",
            7,
        ),
        ("int x; int main() { if (x == 0) x = 5; return x; }", 5),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(0),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        assert!(llvm.contains("@agsem_global0 = internal global i32 0"));
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
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
fn llvm_global_arrays_match_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        eprintln!("LLVM global-array execution skipped: unsupported host tools or target");
        return;
    }
    for (source, expected, error) in [
        ("int a[2]; int main() { return a[0]; }", Some(0), None),
        (
            "int a[2]; int main() { a[0] = 3; a[1] = 4; return a[0] + a[1]; }",
            Some(7),
            None,
        ),
        (
            "int a[3]; int i; int main() { i = 2; a[i] = 7; return a[i]; }",
            Some(7),
            None,
        ),
        (
            "int a[2]; int main() { a[0] += 5; return a[0]; }",
            Some(5),
            None,
        ),
        (
            "int a[2]; int main() { return a[-1]; }",
            None,
            Some("array index out of bounds"),
        ),
        (
            "int a[2]; int main() { a[2] = 1 / 0; return 0; }",
            None,
            Some("division by zero"),
        ),
        (
            "int a[2]; int main() { a[2] += 1 / 0; return 0; }",
            None,
            Some("array index out of bounds"),
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(0),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        assert!(llvm.contains("@agsem_global0 = internal global ["));
        assert!(llvm.contains("getelementptr ["));
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
        if let Some(expected) = expected {
            assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap(),
                Value::Int(expected)
            );
        } else {
            let error = error.unwrap();
            assert_eq!(
                String::from_utf8_lossy(&llvm_result.stderr).trim(),
                error,
                "{source}"
            );
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap_err()
                    .message,
                error
            );
        }
    }
}

#[test]
fn llvm_scalar_calls_match_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        eprintln!("LLVM call execution skipped: unsupported host tools or target");
        return;
    }
    for (source, main_id, expected, error) in [
        (
            "int add(int a, int b) { return a + b; } int main() { return add(2, 3); }",
            1,
            Some(5),
            None,
        ),
        (
            "int add(int a, int b); int twice(int x); int main() { return add(twice(2), twice(3)); } int twice(int x) { return x + x; } int add(int a, int b) { return a + b; }",
            2,
            Some(10),
            None,
        ),
        (
            "int fact(int n) { if (n < 2) return 1; return n * fact(n - 1); } int main() { return fact(5); }",
            1,
            Some(120),
            None,
        ),
        (
            "int g; int bump(int n) { g = g + n; return g; } int main() { return bump(2) + bump(3); }",
            1,
            Some(7),
            None,
        ),
        (
            "int first(int a, int b) { return a; } int main() { return first(1 / 0, 2147483647 + 1); }",
            1,
            None,
            Some("division by zero"),
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(main_id),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        assert!(llvm.contains("define internal i32 @agsem_f1("));
        assert!(llvm.contains("call i32 @agsem_f"));
        let c = backend_c_gen::emit_c(&context, FunctionId(main_id)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
        if let Some(expected) = expected {
            assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap(),
                Value::Int(expected)
            );
        } else {
            let error = error.unwrap();
            assert_eq!(
                String::from_utf8_lossy(&llvm_result.stderr).trim(),
                error,
                "{source}"
            );
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap_err()
                    .message,
                error
            );
        }
    }
}

#[test]
fn llvm_local_arrays_match_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        eprintln!("LLVM array execution skipped: unsupported host tools or target");
        return;
    }
    for (source, expected, error) in [
        (
            "int main() { int a[2]; a[0] = 3; a[1] = 4; return a[0] + a[1]; }",
            Some(7),
            None,
        ),
        (
            "int main() { int a[1]; a[0] = 2; a[0] += 3; return a[0]; }",
            Some(5),
            None,
        ),
        (
            "int main() { int a[2]; return a[0]; }",
            None,
            Some("variable used before initialization"),
        ),
        (
            "int main() { int a[2]; a[-1] = 3; return 0; }",
            None,
            Some("array index out of bounds"),
        ),
        (
            "int main() { int a[2]; a[2] = 1 / 0; return 0; }",
            None,
            Some("division by zero"),
        ),
        (
            "int main() { int a[1]; a[0] += 1 / 0; return 0; }",
            None,
            Some("variable used before initialization"),
        ),
        (
            "int main() { for (int i = 0; i < 2; i = i + 1) { int a[1]; if (i == 0) a[0] = 4; else return a[0]; } return 0; }",
            None,
            Some("variable used before initialization"),
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(0),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        assert!(llvm.contains("_init = alloca ["));
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
        if let Some(expected) = expected {
            assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap(),
                Value::Int(expected)
            );
        } else {
            let error = error.unwrap();
            assert_eq!(
                String::from_utf8_lossy(&llvm_result.stderr).trim(),
                error,
                "{source}"
            );
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap_err()
                    .message,
                error
            );
        }
    }
}

#[test]
fn llvm_bool_and_char_match_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        eprintln!("LLVM execution skipped: unsupported host tools or target");
        return;
    }
    for (source, main_id, expected, error) in [
        (
            "int main() { bool yes = true; if (yes) return 6; return 0; }",
            0,
            Some(6),
            None,
        ),
        (
            "int main() { char c = 'A'; int n = c + 1; return n; }",
            0,
            Some(66),
            None,
        ),
        (
            "bool positive(int x) { return x > 0; } int main() { bool yes = positive(3); if (yes) return 6; return 0; }",
            1,
            Some(6),
            None,
        ),
        (
            "char identity(char c) { return c; } int main() { return identity('A'); }",
            1,
            Some(65),
            None,
        ),
        (
            "int main() { bool a[2]; a[0] = true; if (a[0]) return 8; return 0; }",
            0,
            Some(8),
            None,
        ),
        (
            "int main() { char a[2]; a[0] = 'A'; return a[0]; }",
            0,
            Some(65),
            None,
        ),
        (
            "int main() { char a[1]; a[0] = 'A'; a[0] += 2; return a[0]; }",
            0,
            Some(67),
            None,
        ),
        (
            "bool a[2]; char c; int main() { a[0] = true; c = 'B'; if (a[0]) return c; return 0; }",
            0,
            Some(66),
            None,
        ),
        (
            "int main() { bool a[1]; if (a[0]) return 1; return 0; }",
            0,
            None,
            Some("variable used before initialization"),
        ),
        (
            "int main() { char a[1]; a[1] = 'A'; return 0; }",
            0,
            None,
            Some("array index out of bounds"),
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(main_id),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        let c = backend_c_gen::emit_c(&context, FunctionId(main_id)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
        if let Some(expected) = expected {
            assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap(),
                Value::Int(expected)
            );
        } else {
            let error = error.unwrap();
            assert_eq!(
                String::from_utf8_lossy(&llvm_result.stderr).trim(),
                error,
                "{source}"
            );
            assert_eq!(
                Interpreter::new(&context)
                    .unwrap()
                    .call_named("main", vec![])
                    .unwrap_err()
                    .message,
                error
            );
        }
    }
}

#[test]
fn llvm_float_matches_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        eprintln!("LLVM execution skipped: clang target information unavailable");
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        eprintln!("LLVM execution skipped: unsupported host tools or target");
        return;
    }
    for (source, main_id, expected) in [
        (
            "int main() { float x = 1.5; if (x + 2.0 == 3.5) return 7; return 0; }",
            0,
            7,
        ),
        (
            "float twice(float x) { return x + x; } int main() { float x = 2; if (twice(x) == 4.0) return 9; return 0; }",
            1,
            9,
        ),
        (
            "int main() { if (16777217 == 16777216.0) return 1; return 0; }",
            0,
            1,
        ),
        (
            "int main() { float a[2]; a[0] = 1.5; if (a[0] == 1.5) return 7; return 0; }",
            0,
            7,
        ),
        (
            "float x; int main() { x = 2.5; if (x > 2.0) return 3; return 0; }",
            0,
            3,
        ),
        (
            "int main() { float x = -0.0; if (x == 0.0) return 4; return 0; }",
            0,
            4,
        ),
        (
            "int main() { float x = 1.0 / 0.0; if (x > 0.0) return 5; return 0; }",
            0,
            5,
        ),
        (
            "int main() { float x = 0.0 / 0.0; if (x != x) return 6; return 0; }",
            0,
            6,
        ),
        (
            "int main() { int a[1]; a[0] = 5; a[0] += 0.5; return a[0]; }",
            0,
            5,
        ),
        (
            "int main() { char a[1]; a[0] = 'A'; a[0] += 0.5; return a[0]; }",
            0,
            65,
        ),
        (
            "int main() { int a[1]; a[0] = 0; a[0] += 1.0 / 0.0; if (a[0] == 2147483647) return 11; return 0; }",
            0,
            11,
        ),
        (
            "int main() { char a[1]; a[0] = 'A'; a[0] += 1.0 / 0.0; if (a[0] == 255) return 12; return 0; }",
            0,
            12,
        ),
        (
            "int main() { int a[1]; a[0] = 0; a[0] += 0.0 / 0.0; return a[0]; }",
            0,
            0,
        ),
        (
            "int main() { float x = 0.0 / 0.0; if (x) return 13; return 0; }",
            0,
            13,
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(main_id),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        let c = backend_c_gen::emit_c(&context, FunctionId(main_id)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
        assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
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
fn llvm_float_array_reports_uninitialized_element() {
    let Some((triple, data_layout)) = host_target() else {
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        return;
    }
    let source = "int main() { float a[1]; if (a[0] > 0.0) return 1; return 0; }";
    let context = analyze_source(source).unwrap();
    assert!(context.diagnostics.is_empty(), "{:?}", context.diagnostics);
    let llvm = backend_llvm_gen::emit_llvm(
        &context,
        FunctionId(0),
        TargetSpec {
            triple: &triple,
            data_layout: &data_layout,
        },
    )
    .unwrap();
    let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
    let llvm_result = run_llvm(&llvm);
    let c_result = run_c(&c);
    assert_eq!(llvm_result.status.code(), c_result.status.code());
    assert_eq!(llvm_result.stderr, c_result.stderr);
    assert_eq!(
        String::from_utf8_lossy(&llvm_result.stderr).trim(),
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
fn llvm_struct_field_paths_match_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        return;
    }
    for (source, expected) in [
        (
            "struct Pair { int left; int right; }; int main() { Pair pair; return 0; }",
            0,
        ),
        (
            "struct Pair { int left; int right; }; int main() { Pair pair; pair.left = 2; pair.right = 3; return pair.left + pair.right; }",
            5,
        ),
        (
            "struct Pair { int left; int right; }; Pair pair; int main() { pair.left = 7; return pair.left + pair.right; }",
            7,
        ),
        (
            "struct Pair { int left; int right; }; int main() { Pair pair; pair.left = 2; pair.left += 3; pair.right = 1; return pair.left + pair.right; }",
            6,
        ),
        (
            "struct Mixed { int count; float weight; bool ready; char letter; }; int main() { Mixed item; item.count = 2; item.weight = 1.5; item.ready = true; item.letter = 'A'; if (item.ready) { if (item.weight > 1.0) return item.count + item.letter; } return 0; }",
            67,
        ),
        (
            "struct Inner { int value; char letter; }; struct Outer { int first; Inner inner; int last; }; int main() { Outer item; item.first = 1; item.inner.value = 3; item.inner.letter = 'A'; item.last = 2; return item.first + item.inner.value + item.last + item.inner.letter; }",
            71,
        ),
        (
            "struct Inner { int value; }; struct Outer { Inner inner; int extra; }; Outer item; int main() { item.inner.value = 4; return item.inner.value + item.extra; }",
            4,
        ),
        (
            "struct Inner { int value; }; struct Outer { int first; Inner inner; int last; }; int main() { Outer item; item.inner.value = 9; return item.inner.value; }",
            9,
        ),
        (
            "struct Leaf { int value; }; struct Middle { char tag; Leaf leaf; }; struct Root { int left; Middle middle; }; int main() { Root item; item.middle.leaf.value = 12; return item.middle.leaf.value; }",
            12,
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(0),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
        assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
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
fn llvm_struct_copies_match_c_and_interpreter() {
    let Some((triple, data_layout)) = host_target() else {
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        return;
    }
    for (source, expected) in [
        (
            "struct Pair { int left; int right; }; int main() { Pair source; source.left = 2; source.right = 3; Pair copy = source; return copy.left + copy.right; }",
            5,
        ),
        (
            "struct Pair { int left; int right; }; int main() { Pair source; source.left = 4; source.right = 5; Pair copy; copy = source; copy = copy; return copy.left + copy.right; }",
            9,
        ),
        (
            "struct Pair { int left; int right; }; Pair global; int main() { Pair local; local.left = 6; local.right = 7; global = local; Pair copy = global; return copy.left + copy.right; }",
            13,
        ),
        (
            "struct Inner { int value; }; struct Outer { int first; Inner inner; }; int main() { Outer source; source.first = 1; source.inner.value = 8; Outer copy = source; return copy.first + copy.inner.value; }",
            9,
        ),
        (
            "struct Inner { int value; }; struct Outer { Inner inner; int extra; }; int main() { Inner source; source.value = 11; Outer target; target.inner = source; return target.inner.value; }",
            11,
        ),
    ] {
        let context = analyze_source(source).unwrap();
        assert!(
            context.diagnostics.is_empty(),
            "{source}: {:?}",
            context.diagnostics
        );
        let llvm = backend_llvm_gen::emit_llvm(
            &context,
            FunctionId(0),
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        let c = backend_c_gen::emit_c(&context, FunctionId(0)).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(
            llvm_result.status.code(),
            c_result.status.code(),
            "{source}"
        );
        assert_eq!(llvm_result.stderr, c_result.stderr, "{source}");
        assert_eq!(llvm_result.status.code(), Some(expected), "{source}");
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
fn llvm_aggregate_copy_preserves_partial_initialization_flags() {
    use agsem_runtime::{c_backend, core_ir as core, llvm_backend, structured_ir as structured};

    let Some((triple, data_layout)) = host_target() else {
        return;
    };
    if !triple.starts_with("x86_64-")
        || !triple.contains("-linux-")
        || ["llvm-as", "lli", "opt"]
            .iter()
            .any(|tool| Command::new(tool).arg("--version").output().is_err())
    {
        return;
    }
    let instruction = |result: Option<(usize, usize)>, kind| {
        structured::Statement::Instruction(core::Instruction {
            result: result.map(|(id, ty)| (core::ValueId(id), core::TypeId(ty))),
            kind,
            source: None,
        })
    };
    for (field, expected, error) in [
        (0, Some(7), None),
        (1, None, Some("variable used before initialization")),
    ] {
        let module = structured::Module {
            types: vec![
                core::Type::I32,
                core::Type::Address(core::TypeId(0)),
                core::Type::Aggregate(vec![core::TypeId(0), core::TypeId(0)]),
                core::Type::Address(core::TypeId(2)),
            ],
            globals: vec![],
            functions: vec![structured::Function {
                name: "main".to_owned(),
                parameters: vec![],
                slots: vec![core::TypeId(2), core::TypeId(2)],
                result: Some(core::TypeId(0)),
                body: structured::Block {
                    statements: vec![
                        instruction(None, core::InstructionKind::ResetAggregate(core::SlotId(0))),
                        instruction(None, core::InstructionKind::ResetAggregate(core::SlotId(1))),
                        instruction(
                            Some((0, 3)),
                            core::InstructionKind::SlotAddress(core::SlotId(0)),
                        ),
                        instruction(
                            Some((1, 1)),
                            core::InstructionKind::FieldAddress {
                                base: core::ValueId(0),
                                field: 0,
                            },
                        ),
                        instruction(
                            Some((2, 0)),
                            core::InstructionKind::Constant(core::Constant::I32(7)),
                        ),
                        instruction(
                            None,
                            core::InstructionKind::Store {
                                address: core::ValueId(1),
                                value: core::ValueId(2),
                            },
                        ),
                        instruction(
                            Some((3, 3)),
                            core::InstructionKind::SlotAddress(core::SlotId(1)),
                        ),
                        instruction(
                            None,
                            core::InstructionKind::CopyAggregate {
                                source: core::ValueId(0),
                                target: core::ValueId(3),
                            },
                        ),
                        instruction(
                            Some((4, 1)),
                            core::InstructionKind::FieldAddress {
                                base: core::ValueId(3),
                                field,
                            },
                        ),
                        instruction(Some((5, 0)), core::InstructionKind::Load(core::ValueId(4))),
                        structured::Statement::Return {
                            value: Some(core::ValueId(5)),
                            source: None,
                        },
                    ],
                    source: None,
                },
            }],
            comments: vec![],
        };
        let llvm = llvm_backend::emit_llvm(
            &module,
            TargetSpec {
                triple: &triple,
                data_layout: &data_layout,
            },
        )
        .unwrap();
        let c = c_backend::emit_c(&module).unwrap();
        let llvm_result = run_llvm(&llvm);
        let c_result = run_c(&c);
        assert_eq!(llvm_result.status.code(), c_result.status.code());
        assert_eq!(llvm_result.stderr, c_result.stderr);
        if let Some(expected) = expected {
            assert_eq!(llvm_result.status.code(), Some(expected));
        } else {
            assert_eq!(
                String::from_utf8_lossy(&llvm_result.stderr).trim(),
                error.unwrap()
            );
        }
    }
}

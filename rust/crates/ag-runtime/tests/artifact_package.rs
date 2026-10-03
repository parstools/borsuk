use agas_runtime::artifact::{Manifest, PackageLimits, load_package_directory};
use sha2::{Digest, Sha256};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_DIRECTORY: AtomicU64 = AtomicU64::new(0);

struct ScratchPackage(PathBuf);

impl ScratchPackage {
    fn new() -> Self {
        let index = NEXT_DIRECTORY.fetch_add(1, Ordering::Relaxed);
        let path =
            std::env::temp_dir().join(format!("agas-package-test-{}-{index}", std::process::id()));
        fs::create_dir(&path).expect("create scratch package");
        for entry in fs::read_dir(pinned_package()).expect("list pinned package") {
            let entry = entry.expect("package entry");
            fs::copy(entry.path(), path.join(entry.file_name())).expect("copy package file");
        }
        Self(path)
    }

    fn path(&self) -> &Path {
        &self.0
    }

    fn replace_section(&self, name: &str, bytes: &[u8]) {
        fs::write(self.0.join(name), bytes).expect("write section");
        let manifest_path = self.0.join("manifest.json");
        let mut manifest: Manifest =
            serde_json::from_slice(&fs::read(&manifest_path).expect("manifest bytes"))
                .expect("manifest model");
        let descriptor = manifest
            .sections
            .iter_mut()
            .find(|section| section.file == name)
            .expect("section descriptor");
        descriptor.byte_length = bytes.len() as u64;
        descriptor.sha256 = format!("{:x}", Sha256::digest(bytes));
        let mut encoded = serde_json::to_vec_pretty(&manifest).expect("canonical manifest");
        encoded.push(b'\n');
        fs::write(manifest_path, encoded).expect("write manifest");
    }
}

impl Drop for ScratchPackage {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).expect("remove exact scratch package");
    }
}

fn pinned_package() -> &'static Path {
    Path::new(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/tests/fixtures/ag/v1"
    ))
}

#[test]
fn loads_pinned_ag_package() {
    let package = load_package_directory(pinned_package(), PackageLimits::default())
        .expect("the checked-in C++ package must load in Rust");
    assert_eq!(package.manifest().lookahead, 2);
    assert_eq!(package.manifest().parser_algorithm, "lalr");
    assert_eq!(package.parser_table().view().state_count(), 161);
    assert_eq!(
        package.reductions().instructions.len(),
        package.productions().productions.len()
    );
    assert_eq!(
        package.lexer().rules.len(),
        package.symbols().terminals.len()
    );
    assert_eq!(
        package
            .coverage_productions()
            .expect("pinned coverage metadata")
            .len(),
        package.productions().productions.len()
    );
}

#[test]
fn rejects_coverage_metadata_with_wrong_production_id() {
    let scratch = ScratchPackage::new();
    let section = fs::read(scratch.path().join("diagnostics.json")).expect("diagnostics");
    let mut diagnostics: serde_json::Value = serde_json::from_slice(&section).expect("JSON");
    diagnostics["productionCoverage"][0]["id"] = serde_json::json!(999);
    let mut changed = serde_json::to_vec_pretty(&diagnostics).expect("encode diagnostics");
    changed.push(b'\n');
    scratch.replace_section("diagnostics.json", &changed);
    assert!(load_package_directory(scratch.path(), PackageLimits::default()).is_err());
}

#[test]
fn rejects_missing_extra_and_symlink_sections() {
    let missing = ScratchPackage::new();
    fs::remove_file(missing.path().join("symbols.json")).expect("remove copied section");
    assert!(load_package_directory(missing.path(), PackageLimits::default()).is_err());

    let extra = ScratchPackage::new();
    fs::write(extra.path().join("surprise.json"), b"{}").expect("write extra file");
    assert!(load_package_directory(extra.path(), PackageLimits::default()).is_err());

    #[cfg(unix)]
    {
        let link = ScratchPackage::new();
        fs::remove_file(link.path().join("symbols.json")).expect("remove copied section");
        std::os::unix::fs::symlink(
            pinned_package().join("symbols.json"),
            link.path().join("symbols.json"),
        )
        .expect("create symlink");
        assert!(load_package_directory(link.path(), PackageLimits::default()).is_err());
    }
}

#[test]
fn rejects_hash_length_and_resource_limit_mismatches() {
    let hash = ScratchPackage::new();
    let path = hash.path().join("symbols.json");
    let mut bytes = fs::read(&path).expect("symbols");
    bytes[20] = b'X';
    fs::write(path, bytes).expect("mutate section");
    assert!(load_package_directory(hash.path(), PackageLimits::default()).is_err());

    let truncated = ScratchPackage::new();
    fs::write(truncated.path().join("lexer.json"), b"{").expect("truncate section");
    assert!(load_package_directory(truncated.path(), PackageLimits::default()).is_err());

    let limited = PackageLimits {
        maximum_section_bytes: 100,
        ..PackageLimits::default()
    };
    assert!(load_package_directory(pinned_package(), limited).is_err());
}

#[test]
fn rejects_duplicate_unknown_and_inconsistent_json_after_rehash() {
    let duplicate = ScratchPackage::new();
    let original = fs::read_to_string(duplicate.path().join("symbols.json")).expect("symbols");
    duplicate.replace_section(
        "symbols.json",
        original
            .replacen("\"version\": 1", "\"version\": 1, \"version\": 1", 1)
            .as_bytes(),
    );
    assert!(load_package_directory(duplicate.path(), PackageLimits::default()).is_err());

    let unknown = ScratchPackage::new();
    let original = fs::read_to_string(unknown.path().join("symbols.json")).expect("symbols");
    unknown.replace_section(
        "symbols.json",
        original
            .replacen("\"version\": 1", "\"version\": 1, \"surprise\": 1", 1)
            .as_bytes(),
    );
    assert!(load_package_directory(unknown.path(), PackageLimits::default()).is_err());

    let invalid_reference = ScratchPackage::new();
    let original =
        fs::read_to_string(invalid_reference.path().join("productions.json")).expect("productions");
    invalid_reference.replace_section(
        "productions.json",
        original
            .replacen("\"lhs\": 0", "\"lhs\": 999999", 1)
            .as_bytes(),
    );
    assert!(load_package_directory(invalid_reference.path(), PackageLimits::default()).is_err());
}

#[test]
fn rejects_cross_section_and_parser_mutations_with_valid_hashes() {
    let lexer = ScratchPackage::new();
    let source = fs::read_to_string(lexer.path().join("lexer.json")).expect("lexer");
    lexer.replace_section(
        "lexer.json",
        source
            .replacen("\"terminal\": 0", "\"terminal\": 999999", 1)
            .as_bytes(),
    );
    assert!(load_package_directory(lexer.path(), PackageLimits::default()).is_err());

    let reductions = ScratchPackage::new();
    let source = fs::read_to_string(reductions.path().join("reductions.json")).expect("reductions");
    reductions.replace_section(
        "reductions.json",
        source
            .replacen("\"rhsLength\": 2", "\"rhsLength\": 999999", 1)
            .as_bytes(),
    );
    assert!(load_package_directory(reductions.path(), PackageLimits::default()).is_err());

    let schema = ScratchPackage::new();
    let source = fs::read_to_string(schema.path().join("ast-schema.json")).expect("AST schema");
    schema.replace_section(
        "ast-schema.json",
        source
            .replacen("\"kind\": \"node\"", "\"kind\": \"unknown\"", 1)
            .as_bytes(),
    );
    assert!(load_package_directory(schema.path(), PackageLimits::default()).is_err());

    let parser = ScratchPackage::new();
    let source = fs::read_to_string(parser.path().join("parser.dsl")).expect("parser DSL");
    parser.replace_section(
        "parser.dsl",
        source
            .replacen("start-state 0;", "start-state 999999;", 1)
            .as_bytes(),
    );
    assert!(load_package_directory(parser.path(), PackageLimits::default()).is_err());
}

#[test]
fn rejects_manifest_field_and_filename_mutations() {
    let duplicate = ScratchPackage::new();
    let path = duplicate.path().join("manifest.json");
    let source = fs::read_to_string(&path).expect("manifest");
    fs::write(
        &path,
        source.replacen(
            "\"formatVersion\": 1",
            "\"formatVersion\": 1, \"formatVersion\": 1",
            1,
        ),
    )
    .expect("duplicate manifest field");
    assert!(load_package_directory(duplicate.path(), PackageLimits::default()).is_err());

    let traversal = ScratchPackage::new();
    let path = traversal.path().join("manifest.json");
    let source = fs::read_to_string(&path).expect("manifest");
    fs::write(
        &path,
        source.replacen(
            "\"file\": \"symbols.json\"",
            "\"file\": \"../symbols.json\"",
            1,
        ),
    )
    .expect("traversal descriptor");
    assert!(load_package_directory(traversal.path(), PackageLimits::default()).is_err());
}

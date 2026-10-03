use agas_runtime::artifact::{CoverageProduction, PackageLimits, load_package_directory};
use agas_runtime::ast::{AstParseOutcome, ReductionTraceEntry};
use agas_runtime::lr::RuntimeLimits;
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::env;
use std::error::Error;
use std::fs;
use std::path::{Path, PathBuf};

type QuantifierKey = (String, usize, usize);

fn collect_files(
    path: &Path,
    recursive: bool,
    files: &mut Vec<PathBuf>,
) -> Result<(), Box<dyn Error>> {
    if path.is_file() {
        files.push(path.to_path_buf());
        return Ok(());
    }
    if !path.is_dir() {
        return Err(format!(
            "input is neither a file nor a directory: {}",
            path.display()
        )
        .into());
    }
    for entry in fs::read_dir(path)? {
        let entry = entry?;
        let kind = entry.file_type()?;
        let entry_path = entry.path();
        if kind.is_dir() && recursive {
            collect_files(&entry_path, recursive, files)?;
        } else if kind.is_file() && entry_path.extension().is_some_and(|ext| ext == "vs") {
            files.push(entry_path);
        }
    }
    Ok(())
}

fn cases(repetition: &str) -> &'static [&'static str] {
    match repetition {
        "optional" => &["0", "1"],
        "zero-or-more" => &["0", "1", "2+"],
        "one-or-more" => &["1", "2+"],
        _ => &[],
    }
}

fn case_for(repetition: &str, count: usize) -> &'static str {
    match repetition {
        "optional" if count == 0 => "0",
        "optional" => "1",
        _ if count == 0 => "0",
        _ if count == 1 => "1",
        _ => "2+",
    }
}

#[allow(clippy::too_many_lines)]
fn run() -> Result<bool, Box<dyn Error>> {
    let mut recursive = false;
    let mut paths = Vec::new();
    for arg in env::args().skip(1) {
        if arg == "-r" {
            recursive = true;
        } else {
            paths.push(arg);
        }
    }
    if paths.len() != 2 {
        return Err("usage: agas_coverage [-r] ARTIFACT_DIR FILE_OR_DIRECTORY".into());
    }
    let package = load_package_directory(Path::new(&paths[0]), PackageLimits::default())
        .map_err(|error| format!("cannot load artifact: {error:?}"))?;
    let metadata = package
        .coverage_productions()
        .ok_or("artifact has no productionCoverage metadata; regenerate it with current Agas")?;
    let mut files = Vec::new();
    collect_files(Path::new(&paths[1]), recursive, &mut files)?;
    files.sort();
    if files.is_empty() {
        return Err("no input files found".into());
    }

    let mut quantifiers: BTreeMap<QuantifierKey, (&CoverageProduction, String)> = BTreeMap::new();
    for item in metadata {
        if item.role == "source" || cases(&item.repetition).is_empty() {
            continue;
        }
        let key = (
            item.rule.clone(),
            item.alternative,
            item.element.ok_or("quantifier without element")?,
        );
        let helper = item
            .helper_name
            .as_ref()
            .ok_or("quantifier without helper name")?;
        if let Some((_, previous)) = quantifiers.get(&key) {
            if previous != helper {
                return Err("inconsistent quantifier helper names".into());
            }
        } else {
            quantifiers.insert(key, (item, helper.clone()));
        }
    }
    // Scoped grammar copies share one source quantifier but have distinct LHS IDs.
    let mut helpers: BTreeMap<QuantifierKey, BTreeSet<u32>> = BTreeMap::new();
    for item in metadata.iter().filter(|item| item.role != "source") {
        if let Some(element) = item.element {
            helpers
                .entry((item.rule.clone(), item.alternative, element))
                .or_default()
                .insert(package.productions().productions[item.id as usize].lhs);
        }
    }
    let mut source_quantifiers: HashMap<u32, Vec<(QuantifierKey, usize)>> = HashMap::new();
    for item in metadata.iter().filter(|item| item.role == "source") {
        let production = &package.productions().productions[item.id as usize];
        for (key, _) in quantifiers
            .iter()
            .filter(|(key, _)| key.0 == item.rule && key.1 == item.alternative)
        {
            let helper_ids = helpers.get(key).ok_or("missing helper nonterminal")?;
            let rhs_index = production
                .rhs
                .iter()
                .position(|symbol| symbol.kind == "nonterminal" && helper_ids.contains(&symbol.id))
                .ok_or("source production does not reference quantifier helper")?;
            source_quantifiers
                .entry(item.id)
                .or_default()
                .push((key.clone(), rhs_index));
        }
    }

    let mut covered_alternatives = BTreeSet::new();
    let mut covered_cases: BTreeMap<QuantifierKey, BTreeSet<&'static str>> = BTreeMap::new();
    let mut failures = 0;
    for path in &files {
        let source = fs::read(path)?;
        let mut trace: Vec<ReductionTraceEntry> = Vec::new();
        let parsed = package.parse_with_trace(&source, RuntimeLimits::default(), &mut trace);
        match parsed {
            Ok(result) if matches!(result.outcome, AstParseOutcome::Accepted(_)) => {
                for event in trace {
                    let item = &metadata[event.production_id as usize];
                    if item.role != "source" {
                        continue;
                    }
                    covered_alternatives.insert((item.rule.clone(), item.alternative));
                    if let Some(items) = source_quantifiers.get(&event.production_id) {
                        for (key, rhs_index) in items {
                            let count = event.rhs_cardinalities[*rhs_index];
                            covered_cases
                                .entry(key.clone())
                                .or_default()
                                .insert(case_for(&quantifiers[key].0.repetition, count));
                        }
                    }
                }
            }
            Ok(result) => {
                eprintln!("{}: syntax error: {:?}", path.display(), result.outcome);
                failures += 1;
            }
            Err(error) => {
                eprintln!("{}: parse error: {error:?}", path.display());
                failures += 1;
            }
        }
    }

    let alternatives: BTreeMap<_, _> = metadata
        .iter()
        .filter(|item| item.role == "source")
        .map(|item| ((item.rule.clone(), item.alternative), item))
        .collect();
    println!("files: {}, parse failures: {}", files.len(), failures);
    println!(
        "alternatives: {}/{}",
        covered_alternatives.len(),
        alternatives.len()
    );
    let mut missing = 0;
    for (key, item) in alternatives {
        if !covered_alternatives.contains(&key) {
            println!(
                "missing alternative: {} #{} at {}:{}",
                item.rule,
                item.alternative + 1,
                item.source_line,
                item.source_column
            );
            missing += 1;
        }
    }
    let total_cases: usize = quantifiers
        .values()
        .map(|(item, _)| cases(&item.repetition).len())
        .sum();
    let covered_count: usize = covered_cases.values().map(BTreeSet::len).sum();
    println!("quantifier cases: {covered_count}/{total_cases}");
    for (key, (item, _)) in &quantifiers {
        for case in cases(&item.repetition) {
            if !covered_cases
                .get(key)
                .is_some_and(|seen| seen.contains(case))
            {
                println!(
                    "missing quantifier case {case}: {} #{} element {} at {}:{}",
                    key.0,
                    key.1 + 1,
                    key.2 + 1,
                    item.source_line,
                    item.source_column
                );
                missing += 1;
            }
        }
    }
    Ok(failures == 0 && missing == 0)
}

fn main() {
    match run() {
        Ok(true) => {}
        Ok(false) => std::process::exit(1),
        Err(error) => {
            eprintln!("{error}");
            std::process::exit(2);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{case_for, cases, collect_files};
    use std::fs;

    #[test]
    fn quantifier_cases_require_the_recursive_path() {
        assert_eq!(cases("optional"), ["0", "1"]);
        assert_eq!(cases("one-or-more"), ["1", "2+"]);
        assert_eq!(cases("zero-or-more"), ["0", "1", "2+"]);
        assert_eq!(case_for("zero-or-more", 0), "0");
        assert_eq!(case_for("one-or-more", 1), "1");
        assert_eq!(case_for("one-or-more", 3), "2+");
    }

    #[test]
    fn explicit_file_ignores_extension_and_directory_recursion_is_opt_in() {
        let root = std::env::temp_dir().join(format!("agas-coverage-input-{}", std::process::id()));
        let nested = root.join("nested");
        fs::create_dir_all(&nested).expect("create test directory");
        fs::write(root.join("sample.txt"), "a").expect("create explicit file");
        fs::write(root.join("first.vs"), "a").expect("create direct sample");
        fs::write(nested.join("second.vs"), "a").expect("create nested sample");
        let mut files = Vec::new();
        collect_files(&root.join("sample.txt"), false, &mut files).expect("collect explicit file");
        assert_eq!(files, [root.join("sample.txt")]);
        files.clear();
        collect_files(&root, false, &mut files).expect("collect direct samples");
        assert_eq!(files, [root.join("first.vs")]);
        files.clear();
        collect_files(&root, true, &mut files).expect("collect recursive samples");
        files.sort();
        assert_eq!(files, [root.join("first.vs"), nested.join("second.vs")]);
        fs::remove_dir_all(root).expect("remove test directory");
    }
}

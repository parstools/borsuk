//! Strict, versioned loader for Agas parser packages.

use crate::ast::{AstParseOutcome, AstParser};
use crate::generated::compressed_table::{LEXER_TABLES, PARSER_TABLES, REDUCTION_PROGRAM};
use crate::lexer::Lexer;
use crate::lr::{Parser, RuntimeLimits};
use crate::table_adapter::{
    OwnedParserTable, ParserAlgorithm, ParserTableLimits, ParserTableSymbols,
};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::fs;
use std::path::Path;

const SECTION_KINDS: [(&str, &str); 7] = [
    ("symbols", "symbols.json"),
    ("lexer", "lexer.json"),
    ("parser-table", "parser.dsl"),
    ("productions", "productions.json"),
    ("reductions", "reductions.json"),
    ("ast-schema", "ast-schema.json"),
    ("diagnostics", "diagnostics.json"),
];

#[derive(Clone, Copy, Debug)]
pub struct PackageLimits {
    pub maximum_lookahead: u32,
    pub maximum_manifest_bytes: u64,
    pub maximum_section_bytes: u64,
    pub maximum_package_bytes: u64,
}

impl Default for PackageLimits {
    fn default() -> Self {
        Self {
            maximum_lookahead: 64,
            maximum_manifest_bytes: 1 << 20,
            maximum_section_bytes: 1 << 30,
            maximum_package_bytes: 2 << 30,
        }
    }
}

#[derive(Debug)]
pub enum PackageError {
    Io(std::io::Error),
    Invalid(&'static str),
    Json(serde_json::Error),
}

impl From<std::io::Error> for PackageError {
    fn from(error: std::io::Error) -> Self {
        Self::Io(error)
    }
}

impl From<serde_json::Error> for PackageError {
    fn from(error: serde_json::Error) -> Self {
        Self::Json(error)
    }
}

type Result<T> = std::result::Result<T, PackageError>;

fn invalid<T>(message: &'static str) -> Result<T> {
    Err(PackageError::Invalid(message))
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Manifest {
    pub format_version: u32,
    pub parser_algorithm: String,
    pub lookahead: u32,
    pub start_symbol: String,
    pub root_type: String,
    pub generator_version: String,
    pub zbik_revision: String,
    pub unicode_version: String,
    pub exact_source_sha256: String,
    pub expanded_source_sha256: String,
    pub settings_sha256: String,
    pub sections: Vec<SectionDescriptor>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SectionDescriptor {
    pub kind: String,
    pub version: u32,
    pub file: String,
    pub required: bool,
    pub byte_length: u64,
    pub sha256: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct NamedSymbol {
    pub id: u32,
    pub name: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Symbols {
    pub version: u32,
    pub terminals: Vec<NamedSymbol>,
    pub nonterminals: Vec<NamedSymbol>,
    pub channels: Vec<NamedSymbol>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct SymbolReference {
    pub kind: String,
    pub id: u32,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Production {
    pub id: u32,
    pub lhs: u32,
    pub rhs: Vec<SymbolReference>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Productions {
    pub version: u32,
    pub productions: Vec<Production>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct CodePointRange {
    pub first: u32,
    pub last: u32,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Transition {
    pub ranges: Vec<CodePointRange>,
    pub target: u32,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct LexerRule {
    pub name: String,
    pub terminal: Option<u32>,
    pub channel: Option<u32>,
    pub skipped: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct DfaState {
    pub accepting_rule: Option<u32>,
    pub transitions: Vec<Transition>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct NfaState {
    pub epsilon_transitions: Vec<u32>,
    pub transitions: Vec<Transition>,
    pub ordered_decision: bool,
    pub activates_priority: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct OrderedNfa {
    pub start_state: u32,
    pub accepting_state: u32,
    pub states: Vec<NfaState>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ContextEdge {
    pub terminal: Option<u32>,
    pub target: u32,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ContextNode {
    pub active: u64,
    pub edges: Vec<ContextEdge>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct LexerContext {
    pub original_terminals: Vec<u32>,
    pub required_classes: Vec<u64>,
    pub rows: Vec<Vec<ContextNode>>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct LexerSection {
    pub version: u32,
    pub rules: Vec<LexerRule>,
    pub dfa_states: Vec<DfaState>,
    pub ordered_nfas: Vec<OrderedNfa>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub context: Option<LexerContext>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ReductionField {
    pub name: String,
    pub rhs_index: u32,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct ReductionInstruction {
    pub rule: u32,
    pub rhs_length: u32,
    pub opcode: String,
    pub span_policy: String,
    pub type_name: Option<String>,
    pub variant_name: Option<String>,
    pub operands: Vec<u32>,
    pub fields: Vec<ReductionField>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Reductions {
    pub version: u32,
    pub instructions: Vec<ReductionInstruction>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct AstType {
    pub kind: String,
    pub name: String,
    pub arguments: Vec<AstType>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct AstField {
    pub name: String,
    pub accepted_types: Vec<AstType>,
    pub absent_in_some_alternatives: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct AstAlternative {
    pub source_alternative_index: u32,
    pub variant_name: Option<String>,
    pub result_type: AstType,
    pub fields: Vec<AstField>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct AstRule {
    pub name: String,
    pub tree_modifier: String,
    pub result_types: Vec<AstType>,
    pub public_fields: Vec<AstField>,
    pub alternatives: Vec<AstAlternative>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct AstSchema {
    pub version: u32,
    pub rules: Vec<AstRule>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct CoverageProduction {
    pub id: u32,
    pub stable_identity: String,
    pub rule: String,
    pub alternative: usize,
    pub alternative_label: Option<String>,
    pub element: Option<usize>,
    pub helper_name: Option<String>,
    pub repetition: String,
    pub role: String,
    pub source_line: usize,
    pub source_column: usize,
}

/// A package is returned only after all sections and references pass validation.
pub struct LoadedPackage {
    manifest: Manifest,
    symbols: Symbols,
    lexer: LexerSection,
    parser_table: OwnedParserTable,
    productions: Productions,
    reductions: Reductions,
    ast_schema: AstSchema,
    coverage_productions: Option<Vec<CoverageProduction>>,
}

impl LoadedPackage {
    #[must_use]
    pub fn manifest(&self) -> &Manifest {
        &self.manifest
    }
    #[must_use]
    pub fn symbols(&self) -> &Symbols {
        &self.symbols
    }
    #[must_use]
    pub fn lexer(&self) -> &LexerSection {
        &self.lexer
    }
    #[must_use]
    pub fn parser_table(&self) -> &OwnedParserTable {
        &self.parser_table
    }
    #[must_use]
    pub fn productions(&self) -> &Productions {
        &self.productions
    }
    #[must_use]
    pub fn reductions(&self) -> &Reductions {
        &self.reductions
    }
    #[must_use]
    pub fn ast_schema(&self) -> &AstSchema {
        &self.ast_schema
    }
    #[must_use]
    pub fn coverage_productions(&self) -> Option<&[CoverageProduction]> {
        self.coverage_productions.as_deref()
    }
}

fn sha256(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn hash_is_valid(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn canonical_json<T: Serialize>(value: &T) -> Result<Vec<u8>> {
    let mut bytes = serde_json::to_vec_pretty(value)?;
    bytes.push(b'\n');
    Ok(bytes)
}

fn parse_section<T: for<'a> Deserialize<'a> + Serialize>(bytes: &[u8]) -> Result<T> {
    let value: T = serde_json::from_slice(bytes)?;
    if canonical_json(&value)? != bytes {
        return invalid("noncanonical JSON section");
    }
    Ok(value)
}

fn validate_manifest(manifest: &Manifest, limits: PackageLimits) -> Result<()> {
    if manifest.format_version != 1 {
        return invalid("unsupported manifest version");
    }
    if !matches!(
        manifest.parser_algorithm.as_str(),
        "canonical-lr" | "lalr" | "slr"
    ) {
        return invalid("invalid parser algorithm");
    }
    if manifest.lookahead == 0 || manifest.lookahead > limits.maximum_lookahead {
        return invalid("invalid lookahead");
    }
    if [
        &manifest.start_symbol,
        &manifest.root_type,
        &manifest.generator_version,
        &manifest.zbik_revision,
        &manifest.unicode_version,
    ]
    .iter()
    .any(|value| value.is_empty())
    {
        return invalid("missing package identity");
    }
    if [
        &manifest.exact_source_sha256,
        &manifest.expanded_source_sha256,
        &manifest.settings_sha256,
    ]
    .iter()
    .any(|value| !hash_is_valid(value))
    {
        return invalid("invalid identity hash");
    }
    let mut seen = HashSet::new();
    let mut total = 0_u64;
    let mut previous_kind = None;
    for section in &manifest.sections {
        let Some((kind_index, (_, name))) = SECTION_KINDS
            .iter()
            .enumerate()
            .find(|(_, (kind, _))| *kind == section.kind)
        else {
            return invalid("unknown section kind");
        };
        if previous_kind.is_some_and(|previous| kind_index <= previous) {
            return invalid("manifest sections are not in canonical order");
        }
        previous_kind = Some(kind_index);
        if !seen.insert(section.kind.as_str())
            || section.file != *name
            || (section.version != 1 && !(section.kind == "lexer" && section.version == 2))
            || section.required == (section.kind == "diagnostics")
            || section.byte_length == 0
            || !hash_is_valid(&section.sha256)
        {
            return invalid("invalid or repeated section descriptor");
        }
        total = total
            .checked_add(section.byte_length)
            .ok_or(PackageError::Invalid("package size overflow"))?;
        if section.byte_length > limits.maximum_section_bytes
            || total > limits.maximum_package_bytes
        {
            return invalid("package resource limit exceeded");
        }
    }
    if SECTION_KINDS[..6]
        .iter()
        .any(|(kind, _)| !seen.contains(kind))
    {
        return invalid("missing required section");
    }
    Ok(())
}

fn read_regular_file(path: &Path, maximum: u64, expected: Option<u64>) -> Result<Vec<u8>> {
    let metadata = fs::symlink_metadata(path)?;
    if !metadata.file_type().is_file() {
        return invalid("section is not a regular file");
    }
    if metadata.len() > maximum || expected.is_some_and(|length| length != metadata.len()) {
        return invalid("section length or resource limit mismatch");
    }
    let bytes = fs::read(path)?;
    if bytes.len() as u64 != metadata.len() {
        return invalid("section changed during read");
    }
    Ok(bytes)
}

/// Loads a directory after validating its manifest, hashes, sections, and cross-references.
///
/// # Errors
///
/// Rejects malformed, inconsistent, noncanonical, or unsafe packages.
#[allow(clippy::too_many_lines)]
pub fn load_package_directory(directory: &Path, limits: PackageLimits) -> Result<LoadedPackage> {
    if !fs::symlink_metadata(directory)?.file_type().is_dir() {
        return invalid("package path is not a real directory");
    }
    let manifest_bytes = read_regular_file(
        &directory.join("manifest.json"),
        limits.maximum_manifest_bytes,
        None,
    )?;
    let manifest: Manifest = parse_section(&manifest_bytes)?;
    validate_manifest(&manifest, limits)?;
    let mut sections = std::collections::HashMap::new();
    for descriptor in &manifest.sections {
        let bytes = read_regular_file(
            &directory.join(&descriptor.file),
            limits.maximum_section_bytes,
            Some(descriptor.byte_length),
        )?;
        if sha256(&bytes) != descriptor.sha256 {
            return invalid("section SHA-256 mismatch");
        }
        sections.insert(descriptor.kind.as_str(), bytes);
    }
    let expected_files: HashSet<&str> = manifest
        .sections
        .iter()
        .map(|section| section.file.as_str())
        .chain(std::iter::once("manifest.json"))
        .collect();
    for entry in fs::read_dir(directory)? {
        let entry = entry?;
        let name = entry.file_name();
        if !expected_files.contains(
            name.to_str()
                .ok_or(PackageError::Invalid("non-UTF-8 package entry"))?,
        ) {
            return invalid("undeclared file in package directory");
        }
    }
    let get = |name: &str| -> Result<&[u8]> {
        sections
            .get(name)
            .map(Vec::as_slice)
            .ok_or(PackageError::Invalid("missing section"))
    };
    let symbols: Symbols = parse_section(get("symbols")?)?;
    let productions: Productions = parse_section(get("productions")?)?;
    let lexer: LexerSection = parse_section(get("lexer")?)?;
    let reductions: Reductions = parse_section(get("reductions")?)?;
    let ast_schema: AstSchema = parse_section(get("ast-schema")?)?;
    let coverage_productions: Option<Vec<CoverageProduction>> =
        if sections.contains_key("diagnostics") {
            let diagnostics: serde_json::Value = serde_json::from_slice(get("diagnostics")?)?;
            diagnostics
                .get("productionCoverage")
                .map(|value| serde_json::from_value(value.clone()))
                .transpose()?
        } else {
            None
        };
    validate_symbols(&symbols)?;
    validate_productions(&productions, &symbols)?;
    if let Some(coverage) = &coverage_productions {
        if coverage.len() != productions.productions.len() {
            return invalid("coverage metadata count differs from productions");
        }
        let mut identities = HashSet::new();
        for (id, item) in coverage.iter().enumerate() {
            if item.id as usize != id
                || item.rule.is_empty()
                || item.stable_identity.is_empty()
                || !identities.insert(item.stable_identity.as_str())
                || !matches!(
                    item.repetition.as_str(),
                    "one" | "optional" | "zero-or-more" | "one-or-more"
                )
                || !matches!(
                    item.role.as_str(),
                    "source"
                        | "optional-present"
                        | "optional-empty"
                        | "repetition-recursive"
                        | "repetition-base"
                )
            {
                return invalid("invalid coverage production metadata");
            }
        }
    }
    validate_lexer(&lexer, &symbols)?;
    if manifest
        .sections
        .iter()
        .any(|section| section.kind == "lexer" && section.version != lexer.version)
    {
        return invalid("lexer version differs from manifest");
    }
    if let Some(context) = &lexer.context {
        validate_lexer_context(context, &lexer, &symbols)?;
    }
    validate_reductions(&reductions, &productions)?;
    validate_ast_schema(&ast_schema)?;
    validate_reduction_schema(&reductions, &ast_schema)?;
    if !symbols
        .nonterminals
        .iter()
        .any(|symbol| symbol.name == manifest.start_symbol)
        || !ast_schema
            .rules
            .iter()
            .any(|rule| rule.name == manifest.root_type)
    {
        return invalid("manifest start symbol or root type is missing");
    }
    let parser_bytes = get("parser-table")?;
    let parser_source = std::str::from_utf8(parser_bytes)
        .map_err(|_| PackageError::Invalid("parser DSL is not UTF-8"))?;
    let parser_lexer =
        Lexer::new(&LEXER_TABLES).map_err(|_| PackageError::Invalid("invalid static DSL lexer"))?;
    let tokens = parser_lexer
        .tokenize(parser_bytes)
        .map_err(|_| PackageError::Invalid("invalid parser DSL tokens"))?;
    let parser = Parser::new(&PARSER_TABLES, RuntimeLimits::default())
        .map_err(|_| PackageError::Invalid("invalid static DSL parser"))?;
    let frontend = AstParser::new(parser, &REDUCTION_PROGRAM)
        .map_err(|_| PackageError::Invalid("invalid static DSL reductions"))?;
    let root = match frontend
        .parse(parser_bytes, &tokens)
        .map_err(|_| PackageError::Invalid("invalid parser DSL"))?
    {
        AstParseOutcome::Accepted(root) => root,
        AstParseOutcome::SyntaxError(_) => return invalid("parser DSL syntax error"),
    };
    let terminals: Vec<_> = symbols
        .terminals
        .iter()
        .map(|symbol| symbol.name.as_str())
        .collect();
    let nonterminals: Vec<_> = symbols
        .nonterminals
        .iter()
        .map(|symbol| symbol.name.as_str())
        .collect();
    let table_symbols = ParserTableSymbols {
        terminals: &terminals,
        nonterminals: &nonterminals,
    };
    let table = OwnedParserTable::from_ast(
        &root,
        &table_symbols,
        productions.productions.len(),
        parser_bytes.len(),
        ParserTableLimits {
            maximum_lookahead: limits.maximum_lookahead,
            maximum_section_bytes: limits.maximum_section_bytes,
            ..ParserTableLimits::default()
        },
    )
    .map_err(|_| PackageError::Invalid("invalid parser table"))?;
    if table
        .dump_dsl(&table_symbols)
        .map_err(|_| PackageError::Invalid("cannot serialize parser table"))?
        != parser_source
    {
        return invalid("noncanonical parser DSL");
    }
    let profile = match table.view().algorithm() {
        ParserAlgorithm::Lr => "canonical-lr",
        ParserAlgorithm::Lalr => "lalr",
        ParserAlgorithm::Slr => "slr",
    };
    if manifest.parser_algorithm != profile || manifest.lookahead != table.view().lookahead() {
        return invalid("manifest parser profile differs from table");
    }
    validate_context_table(&lexer, &table, &productions)?;
    Ok(LoadedPackage {
        manifest,
        symbols,
        lexer,
        parser_table: table,
        productions,
        reductions,
        ast_schema,
        coverage_productions,
    })
}

fn validate_names(names: &[NamedSymbol]) -> Result<()> {
    if names.len() > 1_000_000 {
        return invalid("too many symbols");
    }
    let mut seen = HashSet::new();
    for (id, symbol) in names.iter().enumerate() {
        if symbol.id as usize != id
            || symbol.name.is_empty()
            || symbol.name.len() > 1 << 20
            || !seen.insert(symbol.name.as_str())
        {
            return invalid("invalid symbol ID or name");
        }
    }
    Ok(())
}

fn validate_symbols(symbols: &Symbols) -> Result<()> {
    if symbols.version != 1 || symbols.nonterminals.is_empty() {
        return invalid("invalid symbols version or nonterminals");
    }
    validate_names(&symbols.terminals)?;
    validate_names(&symbols.nonterminals)?;
    validate_names(&symbols.channels)
}

fn validate_productions(productions: &Productions, symbols: &Symbols) -> Result<()> {
    if productions.version != 1 || productions.productions.len() > 1_000_000 {
        return invalid("invalid productions version or count");
    }
    let mut rhs_count = 0_usize;
    for (id, production) in productions.productions.iter().enumerate() {
        if production.id as usize != id || production.lhs as usize >= symbols.nonterminals.len() {
            return invalid("invalid production ID or left-hand side");
        }
        rhs_count = rhs_count
            .checked_add(production.rhs.len())
            .ok_or(PackageError::Invalid("RHS count overflow"))?;
        if rhs_count > 10_000_000 {
            return invalid("too many RHS symbols");
        }
        for reference in &production.rhs {
            let count = match reference.kind.as_str() {
                "terminal" => symbols.terminals.len(),
                "nonterminal" => symbols.nonterminals.len(),
                _ => return invalid("invalid symbol reference kind"),
            };
            if reference.id as usize >= count {
                return invalid("production references an unknown symbol");
            }
        }
    }
    Ok(())
}

fn valid_scalar(first: u32, last: u32) -> bool {
    first <= last && last <= 0x0010_ffff && !(first <= 0xdfff && last >= 0xd800)
}

fn validate_transitions(
    transitions: &[Transition],
    states: usize,
    disjoint: bool,
    transition_count: &mut usize,
    range_count: &mut usize,
) -> Result<()> {
    let mut all = Vec::new();
    *transition_count = transition_count
        .checked_add(transitions.len())
        .ok_or(PackageError::Invalid("transition count overflow"))?;
    for transition in transitions {
        if transition.target as usize >= states || transition.ranges.is_empty() {
            return invalid("invalid lexer transition target or empty range");
        }
        *range_count = range_count
            .checked_add(transition.ranges.len())
            .ok_or(PackageError::Invalid("range count overflow"))?;
        let mut previous = None;
        for range in &transition.ranges {
            if !valid_scalar(range.first, range.last)
                || previous.is_some_and(|end| range.first <= end)
            {
                return invalid("invalid or unordered Unicode ranges");
            }
            previous = Some(range.last);
            if disjoint {
                all.push((range.first, range.last));
            }
        }
    }
    if disjoint {
        all.sort_unstable();
        if all.windows(2).any(|pair| pair[1].0 <= pair[0].1) {
            return invalid("overlapping DFA transitions");
        }
    }
    Ok(())
}

fn validate_lexer(lexer: &LexerSection, symbols: &Symbols) -> Result<()> {
    if !matches!(lexer.version, 1 | 2)
        || (lexer.version == 2) != lexer.context.is_some()
        || lexer.rules.is_empty()
        || lexer.dfa_states.is_empty()
        || lexer.rules.len() > 1_000_000
        || lexer.dfa_states.len() > 10_000_000
    {
        return invalid("invalid lexer version, state count, or rule count");
    }
    for rule in &lexer.rules {
        if rule.name.is_empty()
            || rule.skipped == rule.terminal.is_some()
            || rule
                .terminal
                .is_some_and(|id| id as usize >= symbols.terminals.len())
            || rule
                .channel
                .is_some_and(|id| id as usize >= symbols.channels.len())
        {
            return invalid("invalid lexer rule");
        }
    }
    let mut transition_count = 0;
    let mut range_count = 0;
    for state in &lexer.dfa_states {
        if state
            .accepting_rule
            .is_some_and(|id| id as usize >= lexer.rules.len())
        {
            return invalid("DFA accepts unknown rule");
        }
        validate_transitions(
            &state.transitions,
            lexer.dfa_states.len(),
            true,
            &mut transition_count,
            &mut range_count,
        )?;
    }
    if !lexer.ordered_nfas.is_empty() && lexer.ordered_nfas.len() != lexer.rules.len() {
        return invalid("ordered NFA count differs from rule count");
    }
    let mut priority = false;
    for nfa in &lexer.ordered_nfas {
        if nfa.states.is_empty()
            || nfa.states.len() > 10_000_000
            || nfa.start_state as usize >= nfa.states.len()
            || nfa.accepting_state as usize >= nfa.states.len()
        {
            return invalid("invalid ordered NFA endpoints");
        }
        for state in &nfa.states {
            if state.activates_priority && !state.ordered_decision {
                return invalid("NFA priority without decision");
            }
            priority |= state.activates_priority;
            if state
                .epsilon_transitions
                .iter()
                .any(|id| *id as usize >= nfa.states.len())
            {
                return invalid("NFA epsilon transition outside states");
            }
            validate_transitions(
                &state.transitions,
                nfa.states.len(),
                false,
                &mut transition_count,
                &mut range_count,
            )?;
        }
    }
    if !lexer.ordered_nfas.is_empty() && !priority && lexer.context.is_none() {
        return invalid("ordered NFAs lack priority");
    }
    if transition_count > 50_000_000 || range_count > 100_000_000 {
        return invalid("too many lexer transitions");
    }
    Ok(())
}

fn validate_lexer_context(
    context: &LexerContext,
    lexer: &LexerSection,
    symbols: &Symbols,
) -> Result<()> {
    let count = symbols.terminals.len();
    if context.original_terminals.len() != count
        || context.required_classes.len() != lexer.rules.len()
        || context.rows.is_empty()
        || context.rows.len() > 10_000_000
        || lexer.ordered_nfas.len() != lexer.rules.len()
    {
        return invalid("invalid lexer context dimensions");
    }
    for &original in &context.original_terminals {
        if original as usize >= count || context.original_terminals[original as usize] != original {
            return invalid("invalid original terminal mapping");
        }
    }
    for (id, rule) in lexer.rules.iter().enumerate() {
        if ((rule.skipped || rule.channel.is_some()) && context.required_classes[id] != 0)
            || rule
                .terminal
                .is_some_and(|terminal| context.original_terminals[terminal as usize] != terminal)
        {
            return invalid("invalid contextual lexer rule");
        }
    }
    let mut total = 0;
    for row in &context.rows {
        total += row.len();
        if row.is_empty() || total > 10_000_000 {
            return invalid("invalid lexer context tree size");
        }
        let mut reached = vec![false; row.len()];
        reached[0] = true;
        for (id, node) in row.iter().enumerate() {
            if !reached[id] {
                return invalid("unreachable lexer context node");
            }
            let mut terminals = HashSet::new();
            for edge in &node.edges {
                if edge.target as usize <= id
                    || edge.target as usize >= row.len()
                    || reached[edge.target as usize]
                    || edge
                        .terminal
                        .is_some_and(|terminal| terminal as usize >= count)
                    || !terminals.insert(edge.terminal)
                {
                    return invalid("invalid lexer context edge");
                }
                reached[edge.target as usize] = true;
            }
        }
    }
    Ok(())
}

fn validate_context_table(
    lexer: &LexerSection,
    table: &OwnedParserTable,
    productions: &Productions,
) -> Result<()> {
    let Some(context) = &lexer.context else {
        return Ok(());
    };
    let table = table.view();
    if context.rows.len() != table.action_state_rows().len() {
        return invalid("lexer context state count differs from parser");
    }
    for production in &productions.productions {
        for symbol in &production.rhs {
            if symbol.kind == "terminal"
                && context.original_terminals[symbol.id as usize] == symbol.id
            {
                return invalid("contextual parser must use scoped terminals");
            }
        }
    }
    for (state, nodes) in context.rows.iter().enumerate() {
        let row = &table.action_rows()[table.action_state_rows()[state] as usize];
        let mut prefixes: Vec<Vec<crate::lr::LookaheadSymbol>> = vec![Vec::new(); nodes.len()];
        for (id, node) in nodes.iter().enumerate() {
            if prefixes[id].len() > table.lookahead() as usize
                || (!node.edges.is_empty() && prefixes[id].len() == table.lookahead() as usize)
            {
                return invalid("lexer context exceeds lookahead");
            }
            let mut sources = HashSet::new();
            for edge in &node.edges {
                let mut word = prefixes[id].clone();
                word.push(edge.terminal.map_or(
                    crate::lr::LookaheadSymbol::EndOfInput,
                    crate::lr::LookaheadSymbol::Terminal,
                ));
                prefixes[edge.target as usize] = word;
                let Some(terminal) = edge.terminal else {
                    if !nodes[edge.target as usize].edges.is_empty() {
                        return invalid("lexer context continues after EOF");
                    }
                    continue;
                };
                let source = context.original_terminals[terminal as usize];
                if source == terminal || !sources.insert(source) {
                    return invalid("invalid scoped context terminal");
                }
                if !lexer.rules.iter().enumerate().any(|(r, rule)| {
                    rule.terminal == Some(source)
                        && rule.channel.is_none()
                        && context.required_classes[r] & node.active == context.required_classes[r]
                }) {
                    return invalid("no active lexer rule for contextual terminal");
                }
            }
            if node.edges.is_empty() && !prefixes[id].is_empty() {
                if prefixes[id].len() < table.lookahead() as usize
                    && prefixes[id].last() != Some(&crate::lr::LookaheadSymbol::EndOfInput)
                {
                    return invalid("short lexer context without EOF");
                }
                if row.fallback.is_none()
                    && !row
                        .entries
                        .iter()
                        .any(|entry| entry.lookahead == prefixes[id])
                {
                    return invalid("lexer context has no parser action");
                }
            }
        }
        for entry in &row.entries {
            if !prefixes.contains(&entry.lookahead) {
                return invalid("parser action is absent from lexer context");
            }
        }
    }
    Ok(())
}

fn validate_reductions(reductions: &Reductions, productions: &Productions) -> Result<()> {
    if reductions.version != 1 || reductions.instructions.len() != productions.productions.len() {
        return invalid("reduction count or version mismatch");
    }
    for (index, instruction) in reductions.instructions.iter().enumerate() {
        let production = &productions.productions[index];
        if instruction.rule != production.id
            || instruction.rhs_length as usize != production.rhs.len()
        {
            return invalid("reduction does not match production");
        }
        if !matches!(
            instruction.span_policy.as_str(),
            "matched-rhs" | "empty-at-lookahead"
        ) || (instruction.rhs_length == 0) != (instruction.span_policy == "empty-at-lookahead")
        {
            return invalid("invalid reduction span policy");
        }
        let expected = match instruction.opcode.as_str() {
            "forward" | "construct-node-or-forward" | "optional-some" | "list-singleton" => 1,
            "list-append" => 2,
            "unit" | "construct-node" | "construct-record" | "optional-none" | "list-empty" => 0,
            _ => return invalid("invalid reduction opcode"),
        };
        if instruction.operands.len() != expected
            || instruction
                .operands
                .iter()
                .any(|id| *id >= instruction.rhs_length)
        {
            return invalid("invalid reduction operands");
        }
        let constructs = matches!(
            instruction.opcode.as_str(),
            "construct-node" | "construct-node-or-forward" | "construct-record"
        );
        if constructs
            != instruction
                .type_name
                .as_ref()
                .is_some_and(|name| !name.is_empty())
            || (instruction.variant_name.is_some()
                && !matches!(
                    instruction.opcode.as_str(),
                    "construct-node" | "construct-node-or-forward"
                ))
            || (!constructs && !instruction.fields.is_empty())
        {
            return invalid("invalid reduction type or fields");
        }
        if instruction.opcode == "construct-node-or-forward"
            && (instruction.fields.len() < 2
                || instruction
                    .fields
                    .iter()
                    .filter(|field| field.rhs_index == instruction.operands[0])
                    .count()
                    != 1)
        {
            return invalid("invalid conditional forwarding operand");
        }
        let mut names = HashSet::new();
        for field in &instruction.fields {
            if field.name.is_empty()
                || !names.insert(field.name.as_str())
                || field.rhs_index >= instruction.rhs_length
            {
                return invalid("invalid reduction field");
            }
        }
    }
    Ok(())
}

fn validate_type(value: &AstType, rules: &HashSet<&str>, depth: usize) -> Result<()> {
    if depth > 128 {
        return invalid("AST type nesting too deep");
    }
    let container = matches!(value.kind.as_str(), "optional" | "list" | "non-empty-list");
    let choice = value.kind == "choice";
    if !matches!(
        value.kind.as_str(),
        "unit"
            | "token"
            | "rule"
            | "node"
            | "record"
            | "optional"
            | "list"
            | "non-empty-list"
            | "choice"
    ) || (choice && value.arguments.len() < 2)
        || (!choice && value.arguments.len() != usize::from(container))
        || ((container || choice) && !value.name.is_empty())
        || (!container && !choice && (value.kind == "unit") != value.name.is_empty())
        || (value.kind == "rule" && !rules.contains(value.name.as_str()))
    {
        return invalid("invalid AST type reference or shape");
    }
    for argument in &value.arguments {
        validate_type(argument, rules, depth + 1)?;
    }
    Ok(())
}

fn validate_fields(fields: &[AstField], rules: &HashSet<&str>) -> Result<()> {
    let mut names = HashSet::new();
    for field in fields {
        if field.name.is_empty()
            || !names.insert(field.name.as_str())
            || field.accepted_types.is_empty()
        {
            return invalid("invalid or duplicate AST field");
        }
        for kind in &field.accepted_types {
            validate_type(kind, rules, 0)?;
        }
    }
    Ok(())
}

fn validate_ast_schema(schema: &AstSchema) -> Result<()> {
    if schema.version != 1 {
        return invalid("unsupported AST schema version");
    }
    let rules: HashSet<&str> = schema.rules.iter().map(|rule| rule.name.as_str()).collect();
    if rules.len() != schema.rules.len() || rules.contains("") {
        return invalid("duplicate or empty AST rule");
    }
    for rule in &schema.rules {
        if !matches!(rule.tree_modifier.as_str(), "node" | "inline") {
            return invalid("invalid AST tree modifier");
        }
        for kind in &rule.result_types {
            validate_type(kind, &rules, 0)?;
        }
        validate_fields(&rule.public_fields, &rules)?;
        for (id, alternative) in rule.alternatives.iter().enumerate() {
            if alternative.source_alternative_index as usize != id {
                return invalid("AST alternatives are not dense");
            }
            validate_type(&alternative.result_type, &rules, 0)?;
            validate_fields(&alternative.fields, &rules)?;
        }
    }
    Ok(())
}

fn validate_reduction_schema(reductions: &Reductions, schema: &AstSchema) -> Result<()> {
    for instruction in &reductions.instructions {
        let Some(type_name) = &instruction.type_name else {
            continue;
        };
        let Some(rule) = schema.rules.iter().find(|rule| rule.name == *type_name) else {
            return invalid("reduction constructs an unknown AST type");
        };
        if instruction.variant_name.as_ref().is_some_and(|variant| {
            !rule
                .alternatives
                .iter()
                .any(|alternative| alternative.variant_name.as_ref() == Some(variant))
        }) {
            return invalid("reduction selects an unknown AST variant");
        }
        for field in &instruction.fields {
            if !rule
                .public_fields
                .iter()
                .any(|candidate| candidate.name == field.name)
                && !rule.alternatives.iter().any(|alternative| {
                    alternative
                        .fields
                        .iter()
                        .any(|candidate| candidate.name == field.name)
                })
            {
                return invalid("reduction binds an unknown AST field");
            }
        }
    }
    Ok(())
}

//! Convert the neutral `CompressedTable` AST into a validated owned LR table.

use crate::ast::{AstValue, AstValueKind};
use crate::lr::{Action, LookaheadSymbol};
use std::collections::{HashMap, HashSet};
use std::fmt::Write;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ParserAlgorithm {
    Lr,
    Lalr,
    Slr,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OwnedActionEntry {
    pub lookahead: Vec<LookaheadSymbol>,
    pub action: Action,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OwnedActionRow {
    pub entries: Vec<OwnedActionEntry>,
    pub fallback: Option<u32>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct OwnedGotoEntry {
    pub nonterminal: u32,
    pub state: u32,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OwnedParserTable {
    algorithm: ParserAlgorithm,
    lookahead: u32,
    start_state: u32,
    action_rows: Vec<OwnedActionRow>,
    action_state_rows: Vec<u32>,
    goto_rows: Vec<Vec<OwnedGotoEntry>>,
    goto_state_rows: Vec<u32>,
}

/// Dense symbol names in the same identifier order as the artifact catalog.
pub struct ParserTableSymbols<'a> {
    pub terminals: &'a [&'a str],
    pub nonterminals: &'a [&'a str],
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ParserTableLimits {
    pub maximum_section_bytes: u64,
    pub maximum_lookahead: u32,
    pub maximum_states: usize,
    pub maximum_rows: usize,
    pub maximum_entries: usize,
    pub maximum_name_bytes: usize,
}

impl Default for ParserTableLimits {
    fn default() -> Self {
        Self {
            maximum_section_bytes: 1 << 30,
            maximum_lookahead: 64,
            maximum_states: 10_000_000,
            maximum_rows: 10_000_000,
            maximum_entries: 100_000_000,
            maximum_name_bytes: 1 << 20,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum TableAdapterError {
    InvalidAst(&'static str),
    InvalidAlgorithm,
    InvalidId(&'static str),
    DuplicateEntry(&'static str),
    InvalidReference(&'static str),
    ResourceLimit(&'static str),
}

impl OwnedParserTable {
    /// Builds an owned table only after every AST field and cross-reference is validated.
    ///
    /// # Errors
    ///
    /// Returns a typed error for malformed AST data, references, or limits.
    #[allow(clippy::too_many_lines)]
    pub fn from_ast(
        root: &AstValue,
        symbols: &ParserTableSymbols<'_>,
        production_count: usize,
        source_bytes: usize,
        limits: ParserTableLimits,
    ) -> Result<Self, TableAdapterError> {
        let root = expect_node(root, "document")?;
        let source_length = u64::try_from(source_bytes)
            .map_err(|_| TableAdapterError::ResourceLimit("table section is too large"))?;
        if source_length > limits.maximum_section_bytes {
            return Err(TableAdapterError::ResourceLimit(
                "table section is too large",
            ));
        }
        if root.source_span.end_byte > source_length {
            return Err(TableAdapterError::InvalidAst("AST range exceeds source"));
        }
        let (algorithm, lookahead) = parse_algorithm(&json_string(field(root, "parserName")?)?)?;
        if lookahead > limits.maximum_lookahead {
            return Err(TableAdapterError::ResourceLimit("lookahead is too large"));
        }
        let terminal_ids = symbol_ids(symbols.terminals)?;
        let nonterminal_ids = symbol_ids(symbols.nonterminals)?;
        let start_state = uint(field(root, "startState")?)?;

        let mut action_rows = Vec::new();
        let mut entry_count = 0_usize;
        for row in list(field(root, "actionRows")?)? {
            if action_rows.len() >= limits.maximum_rows {
                return Err(TableAdapterError::ResourceLimit("too many action rows"));
            }
            let row = expect_node(row, "actionRow")?;
            if uint(field(row, "id")?)? as usize != action_rows.len() {
                return Err(TableAdapterError::InvalidId("action row IDs are not dense"));
            }
            let row_entries = list(field(row, "entries")?)?;
            entry_count = entry_count
                .checked_add(row_entries.len())
                .ok_or(TableAdapterError::ResourceLimit("too many table entries"))?;
            if entry_count > limits.maximum_entries {
                return Err(TableAdapterError::ResourceLimit("too many table entries"));
            }
            let mut entries = Vec::new();
            for entry in row_entries {
                let entry = expect_node(entry, "actionEntry")?;
                let key = expect_node(field(entry, "key")?, "lookahead")?;
                let rest = list(field(key, "rest")?)?;
                if rest.len() >= lookahead as usize {
                    return Err(TableAdapterError::InvalidReference(
                        "invalid lookahead length",
                    ));
                }
                let mut lookahead_word = Vec::new();
                lookahead_word.push(lookahead_symbol(
                    field(key, "first")?,
                    &terminal_ids,
                    limits.maximum_name_bytes,
                )?);
                for symbol in rest {
                    lookahead_word.push(lookahead_symbol(
                        symbol,
                        &terminal_ids,
                        limits.maximum_name_bytes,
                    )?);
                }
                entries.push(OwnedActionEntry {
                    lookahead: lookahead_word,
                    action: parse_action(field(entry, "value")?)?,
                });
            }
            let default = expect_node(field(row, "fallback")?, "defaultAction")?;
            let fallback = match default.variant_name.as_str() {
                "DefaultReduction" => Some(uint(field(default, "ruleId")?)?),
                "DefaultError" => None,
                _ => return Err(TableAdapterError::InvalidAst("unknown default action")),
            };
            action_rows.push(OwnedActionRow { entries, fallback });
        }
        let action_state_rows = indices(field(root, "actionStateRows")?, limits.maximum_states)?;

        let mut goto_rows = Vec::new();
        for row in list(field(root, "gotoRows")?)? {
            if goto_rows.len() >= limits.maximum_rows {
                return Err(TableAdapterError::ResourceLimit("too many goto rows"));
            }
            let row = expect_node(row, "gotoRow")?;
            if uint(field(row, "id")?)? as usize != goto_rows.len() {
                return Err(TableAdapterError::InvalidId("goto row IDs are not dense"));
            }
            let row_entries = list(field(row, "entries")?)?;
            entry_count = entry_count
                .checked_add(row_entries.len())
                .ok_or(TableAdapterError::ResourceLimit("too many table entries"))?;
            if entry_count > limits.maximum_entries {
                return Err(TableAdapterError::ResourceLimit("too many table entries"));
            }
            let mut entries = Vec::new();
            for entry in row_entries {
                let entry = expect_node(entry, "gotoEntry")?;
                let name = json_string(field(entry, "nonterminal")?)?;
                if name.len() > limits.maximum_name_bytes {
                    return Err(TableAdapterError::ResourceLimit(
                        "nonterminal name is too long",
                    ));
                }
                let nonterminal = *nonterminal_ids.get(name.as_str()).ok_or(
                    TableAdapterError::InvalidReference("unknown GOTO nonterminal"),
                )?;
                entries.push(OwnedGotoEntry {
                    nonterminal,
                    state: uint(field(entry, "stateId")?)?,
                });
            }
            goto_rows.push(entries);
        }
        let goto_state_rows = indices(field(root, "gotoStateRows")?, limits.maximum_states)?;
        let table = Self {
            algorithm,
            lookahead,
            start_state,
            action_rows,
            action_state_rows,
            goto_rows,
            goto_state_rows,
        };
        table.validate(symbols, production_count, limits)?;
        Ok(table)
    }

    /// Returns a read-only view of a table that has passed complete validation.
    #[must_use]
    pub fn view(&self) -> ParserTableView<'_> {
        ParserTableView { table: self }
    }

    /// Reproduces the canonical DSL emitted by the C++ artifact writer.
    ///
    /// # Errors
    ///
    /// Returns an error if a referenced identifier is absent from the
    /// supplied symbol catalog. Pass the same catalog used for construction.
    #[allow(clippy::too_many_lines)]
    pub fn dump_dsl(&self, symbols: &ParserTableSymbols<'_>) -> Result<String, TableAdapterError> {
        let parser_name = match self.algorithm {
            ParserAlgorithm::Lr => format!("LR({})", self.lookahead),
            ParserAlgorithm::Lalr => format!("LALR({})", self.lookahead),
            ParserAlgorithm::Slr => "SLR".to_owned(),
        };
        let mut out = format!(
            "compressed-table {} {{\n  start-state {};\n\n",
            quote(&parser_name),
            self.start_state,
        );
        for (id, row) in self.action_rows.iter().enumerate() {
            writeln!(out, "  action-row {id} {{").expect("string write");
            let mut entries: Vec<_> = row.entries.iter().collect();
            entries.sort_by(|left, right| {
                left.lookahead
                    .iter()
                    .map(|symbol| symbol_sort_key(*symbol))
                    .cmp(
                        right
                            .lookahead
                            .iter()
                            .map(|symbol| symbol_sort_key(*symbol)),
                    )
            });
            for entry in entries {
                out.push_str("    [");
                for (index, symbol) in entry.lookahead.iter().enumerate() {
                    if index != 0 {
                        out.push_str(", ");
                    }
                    match symbol {
                        LookaheadSymbol::Terminal(id) => {
                            let name = symbols.terminals.get(*id as usize).ok_or(
                                TableAdapterError::InvalidReference("unknown terminal in catalog"),
                            )?;
                            out.push_str(&quote(name));
                        }
                        LookaheadSymbol::EndOfInput => out.push_str("EOF"),
                    }
                }
                out.push_str("] => ");
                out.push_str(&format_action(entry.action));
                out.push_str(";\n");
            }
            out.push_str("    any => ");
            match row.fallback {
                Some(rule) => write!(out, "reduce {rule}").expect("string write"),
                None => out.push_str("error"),
            }
            out.push_str(";\n  }\n");
        }
        out.push_str("\n  action-state-rows [");
        append_indices(&mut out, &self.action_state_rows);
        out.push_str("];\n\n");
        for (id, row) in self.goto_rows.iter().enumerate() {
            writeln!(out, "  goto-row {id} {{").expect("string write");
            let mut entries = row.clone();
            entries.sort_by_key(|entry| entry.nonterminal);
            for entry in entries {
                let name = symbols.nonterminals.get(entry.nonterminal as usize).ok_or(
                    TableAdapterError::InvalidReference("unknown nonterminal in catalog"),
                )?;
                writeln!(out, "    {} => {};", quote(name), entry.state).expect("string write");
            }
            out.push_str("  }\n");
        }
        out.push_str("\n  goto-state-rows [");
        append_indices(&mut out, &self.goto_state_rows);
        out.push_str("];\n}\n");
        Ok(out)
    }

    #[allow(clippy::too_many_lines)]
    fn validate(
        &self,
        symbols: &ParserTableSymbols<'_>,
        production_count: usize,
        limits: ParserTableLimits,
    ) -> Result<(), TableAdapterError> {
        let state_count = self.action_state_rows.len();
        if state_count == 0 || state_count != self.goto_state_rows.len() {
            return Err(TableAdapterError::InvalidId(
                "state mappings have unequal sizes",
            ));
        }
        if state_count > limits.maximum_states {
            return Err(TableAdapterError::ResourceLimit("too many states"));
        }
        if self.action_rows.is_empty() || self.goto_rows.is_empty() {
            return Err(TableAdapterError::InvalidId("row pools must be nonempty"));
        }
        if self.start_state as usize >= state_count {
            return Err(TableAdapterError::InvalidReference("invalid start state"));
        }
        let mut total_entries = 0_usize;
        for row in &self.action_rows {
            if row
                .fallback
                .is_some_and(|id| id as usize >= production_count)
            {
                return Err(TableAdapterError::InvalidReference(
                    "invalid default reduction",
                ));
            }
            total_entries = total_entries
                .checked_add(row.entries.len())
                .ok_or(TableAdapterError::ResourceLimit("too many table entries"))?;
            if total_entries > limits.maximum_entries {
                return Err(TableAdapterError::ResourceLimit("too many table entries"));
            }
            let mut keys = HashSet::new();
            for entry in &row.entries {
                let word = &entry.lookahead;
                if word.is_empty() || word.len() > self.lookahead as usize {
                    return Err(TableAdapterError::InvalidReference(
                        "invalid lookahead length",
                    ));
                }
                let mut saw_eof = false;
                for (index, symbol) in word.iter().enumerate() {
                    match symbol {
                        LookaheadSymbol::Terminal(id)
                            if *id as usize >= symbols.terminals.len() =>
                        {
                            return Err(TableAdapterError::InvalidReference("unknown terminal"));
                        }
                        LookaheadSymbol::EndOfInput => {
                            if index + 1 != word.len() {
                                return Err(TableAdapterError::InvalidReference("EOF is not last"));
                            }
                            saw_eof = true;
                        }
                        LookaheadSymbol::Terminal(_) => {}
                    }
                }
                if word.len() < self.lookahead as usize && !saw_eof {
                    return Err(TableAdapterError::InvalidReference(
                        "short lookahead lacks EOF",
                    ));
                }
                if !keys.insert(word.as_slice()) {
                    return Err(TableAdapterError::DuplicateEntry("duplicate lookahead"));
                }
                match entry.action {
                    Action::Shift(target)
                        if target as usize >= state_count
                            || matches!(word[0], LookaheadSymbol::EndOfInput) =>
                    {
                        return Err(TableAdapterError::InvalidReference("invalid shift"));
                    }
                    Action::Reduce(rule) if rule as usize >= production_count => {
                        return Err(TableAdapterError::InvalidReference("invalid reduction"));
                    }
                    Action::Accept if !saw_eof => {
                        return Err(TableAdapterError::InvalidReference("accept lacks EOF"));
                    }
                    _ => {}
                }
            }
        }
        if self
            .action_state_rows
            .iter()
            .any(|id| *id as usize >= self.action_rows.len())
        {
            return Err(TableAdapterError::InvalidReference("unknown action row"));
        }
        for row in &self.goto_rows {
            total_entries = total_entries
                .checked_add(row.len())
                .ok_or(TableAdapterError::ResourceLimit("too many table entries"))?;
            if total_entries > limits.maximum_entries {
                return Err(TableAdapterError::ResourceLimit("too many table entries"));
            }
            let mut keys = HashSet::new();
            for entry in row {
                if entry.nonterminal as usize >= symbols.nonterminals.len()
                    || entry.state as usize >= state_count
                {
                    return Err(TableAdapterError::InvalidReference(
                        "invalid GOTO reference",
                    ));
                }
                if !keys.insert(entry.nonterminal) {
                    return Err(TableAdapterError::DuplicateEntry(
                        "duplicate GOTO nonterminal",
                    ));
                }
            }
        }
        if self
            .goto_state_rows
            .iter()
            .any(|id| *id as usize >= self.goto_rows.len())
        {
            return Err(TableAdapterError::InvalidReference("unknown GOTO row"));
        }
        Ok(())
    }
}

fn symbol_sort_key(symbol: LookaheadSymbol) -> u64 {
    match symbol {
        LookaheadSymbol::Terminal(id) => u64::from(id),
        LookaheadSymbol::EndOfInput => u64::from(u32::MAX) + 1,
    }
}

fn format_action(action: Action) -> String {
    match action {
        Action::Shift(state) => format!("shift {state}"),
        Action::Reduce(rule) => format!("reduce {rule}"),
        Action::Accept => "accept".to_owned(),
    }
}

fn append_indices(out: &mut String, indices: &[u32]) {
    for (index, value) in indices.iter().enumerate() {
        if index != 0 {
            out.push_str(", ");
        }
        write!(out, "{value}").expect("string write");
    }
}

fn quote(value: &str) -> String {
    let mut result = String::from("\"");
    for character in value.chars() {
        match character {
            '\\' => result.push_str("\\\\"),
            '"' => result.push_str("\\\""),
            '\n' => result.push_str("\\n"),
            '\r' => result.push_str("\\r"),
            '\t' => result.push_str("\\t"),
            ch if ch < ' ' => write!(result, "\\u{:04x}", u32::from(ch)).expect("string write"),
            ch => result.push(ch),
        }
    }
    result.push('"');
    result
}

/// Runtime-facing accessors cannot observe an unvalidated table.
#[derive(Clone, Copy)]
pub struct ParserTableView<'a> {
    table: &'a OwnedParserTable,
}

impl ParserTableView<'_> {
    #[must_use]
    pub fn algorithm(&self) -> ParserAlgorithm {
        self.table.algorithm
    }
    #[must_use]
    pub fn lookahead(&self) -> u32 {
        self.table.lookahead
    }
    #[must_use]
    pub fn start_state(&self) -> u32 {
        self.table.start_state
    }
    #[must_use]
    pub fn state_count(&self) -> usize {
        self.table.action_state_rows.len()
    }
    #[must_use]
    pub fn action_row(&self, state: u32) -> Option<&OwnedActionRow> {
        self.table
            .action_state_rows
            .get(state as usize)
            .map(|row| &self.table.action_rows[*row as usize])
    }
    #[must_use]
    pub fn goto_row(&self, state: u32) -> Option<&[OwnedGotoEntry]> {
        self.table
            .goto_state_rows
            .get(state as usize)
            .map(|row| self.table.goto_rows[*row as usize].as_slice())
    }
    #[must_use]
    pub fn action(&self, state: u32, word: &[LookaheadSymbol]) -> Option<Action> {
        let row = self.action_row(state)?;
        row.entries
            .iter()
            .find(|entry| entry.lookahead == word)
            .map(|entry| entry.action)
            .or_else(|| row.fallback.map(Action::Reduce))
    }
    #[must_use]
    pub fn goto(&self, state: u32, nonterminal: u32) -> Option<u32> {
        self.goto_row(state)?
            .iter()
            .find(|entry| entry.nonterminal == nonterminal)
            .map(|entry| entry.state)
    }
    #[must_use]
    pub fn action_row_count(&self) -> usize {
        self.table.action_rows.len()
    }
    #[must_use]
    pub fn action_rows(&self) -> &[OwnedActionRow] {
        &self.table.action_rows
    }
    #[must_use]
    pub fn goto_row_count(&self) -> usize {
        self.table.goto_rows.len()
    }
    #[must_use]
    pub fn goto_rows(&self) -> &[Vec<OwnedGotoEntry>] {
        &self.table.goto_rows
    }
    #[must_use]
    pub fn action_state_rows(&self) -> &[u32] {
        &self.table.action_state_rows
    }
    #[must_use]
    pub fn goto_state_rows(&self) -> &[u32] {
        &self.table.goto_state_rows
    }
}

fn expect_node<'a>(value: &'a AstValue, name: &str) -> Result<&'a AstValue, TableAdapterError> {
    if value.kind != AstValueKind::Node || value.type_name != name {
        return Err(TableAdapterError::InvalidAst("unexpected AST node"));
    }
    if value.field_names.len() != value.elements.len() {
        return Err(TableAdapterError::InvalidAst(
            "node fields are inconsistent",
        ));
    }
    Ok(value)
}

fn field<'a>(value: &'a AstValue, name: &str) -> Result<&'a AstValue, TableAdapterError> {
    let mut matches = value
        .field_names
        .iter()
        .zip(&value.elements)
        .filter(|(field_name, _)| field_name.as_str() == name);
    let result = matches
        .next()
        .map(|(_, item)| item)
        .ok_or(TableAdapterError::InvalidAst("missing AST field"))?;
    if matches.next().is_some() {
        return Err(TableAdapterError::InvalidAst("duplicate AST field"));
    }
    Ok(result)
}

fn list(value: &AstValue) -> Result<&[AstValue], TableAdapterError> {
    if value.kind != AstValueKind::List {
        return Err(TableAdapterError::InvalidAst("expected AST list"));
    }
    Ok(&value.elements)
}

fn uint(value: &AstValue) -> Result<u32, TableAdapterError> {
    if value.kind != AstValueKind::Token {
        return Err(TableAdapterError::InvalidAst("expected UINT token"));
    }
    let text = value.token_text.as_str();
    if text.is_empty()
        || (text.len() > 1 && text.starts_with('0'))
        || !text.bytes().all(|byte| byte.is_ascii_digit())
    {
        return Err(TableAdapterError::InvalidId("invalid unsigned integer"));
    }
    text.parse()
        .map_err(|_| TableAdapterError::InvalidId("unsigned integer exceeds u32"))
}

fn indices(value: &AstValue, maximum_states: usize) -> Result<Vec<u32>, TableAdapterError> {
    let value = expect_node(value, "indices")?;
    let rest = list(field(value, "rest")?)?;
    if rest.len() >= maximum_states {
        return Err(TableAdapterError::ResourceLimit("too many states"));
    }
    let mut result = vec![uint(field(value, "first")?)?];
    for token in rest {
        result.push(uint(token)?);
    }
    Ok(result)
}

fn symbol_ids<'a>(names: &'a [&'a str]) -> Result<HashMap<&'a str, u32>, TableAdapterError> {
    let mut result = HashMap::with_capacity(names.len());
    for (id, name) in names.iter().copied().enumerate() {
        let id =
            u32::try_from(id).map_err(|_| TableAdapterError::ResourceLimit("too many symbols"))?;
        if result.insert(name, id).is_some() {
            return Err(TableAdapterError::DuplicateEntry("duplicate symbol name"));
        }
    }
    Ok(result)
}

fn lookahead_symbol(
    value: &AstValue,
    terminals: &HashMap<&str, u32>,
    maximum_name_bytes: usize,
) -> Result<LookaheadSymbol, TableAdapterError> {
    let value = expect_node(value, "lookaheadSymbol")?;
    match value.variant_name.as_str() {
        "EndOfInputSymbol" => Ok(LookaheadSymbol::EndOfInput),
        "TerminalSymbol" => {
            let name = json_string(field(value, "value")?)?;
            if name.len() > maximum_name_bytes {
                return Err(TableAdapterError::ResourceLimit(
                    "terminal name is too long",
                ));
            }
            let id = terminals
                .get(name.as_str())
                .ok_or(TableAdapterError::InvalidReference("unknown terminal"))?;
            Ok(LookaheadSymbol::Terminal(*id))
        }
        _ => Err(TableAdapterError::InvalidAst("unknown lookahead symbol")),
    }
}

fn parse_action(value: &AstValue) -> Result<Action, TableAdapterError> {
    let value = expect_node(value, "action")?;
    match value.variant_name.as_str() {
        "ShiftAction" => Ok(Action::Shift(uint(field(value, "stateId")?)?)),
        "ReduceAction" => Ok(Action::Reduce(uint(field(value, "ruleId")?)?)),
        "AcceptAction" => Ok(Action::Accept),
        _ => Err(TableAdapterError::InvalidAst("unknown parser action")),
    }
}

fn parse_algorithm(name: &str) -> Result<(ParserAlgorithm, u32), TableAdapterError> {
    if name == "SLR" {
        return Ok((ParserAlgorithm::Slr, 1));
    }
    for (prefix, algorithm) in [
        ("LR(", ParserAlgorithm::Lr),
        ("LALR(", ParserAlgorithm::Lalr),
    ] {
        if let Some(number) = name
            .strip_prefix(prefix)
            .and_then(|tail| tail.strip_suffix(')'))
        {
            if number.is_empty()
                || (number.len() > 1 && number.starts_with('0'))
                || !number.bytes().all(|byte| byte.is_ascii_digit())
            {
                return Err(TableAdapterError::InvalidAlgorithm);
            }
            let lookahead = number
                .parse::<u32>()
                .map_err(|_| TableAdapterError::InvalidAlgorithm)?;
            if lookahead == 0 {
                return Err(TableAdapterError::InvalidAlgorithm);
            }
            return Ok((algorithm, lookahead));
        }
    }
    Err(TableAdapterError::InvalidAlgorithm)
}

fn json_string(value: &AstValue) -> Result<String, TableAdapterError> {
    if value.kind != AstValueKind::Token {
        return Err(TableAdapterError::InvalidAst("expected STRING token"));
    }
    let raw = value.token_text.as_str();
    let Some(body) = raw
        .strip_prefix('"')
        .and_then(|part| part.strip_suffix('"'))
    else {
        return Err(TableAdapterError::InvalidAst("invalid JSON string token"));
    };
    let mut chars = body.chars();
    let mut result = String::new();
    while let Some(ch) = chars.next() {
        if ch != '\\' {
            if ch < ' ' {
                return Err(TableAdapterError::InvalidAst("control character in string"));
            }
            result.push(ch);
            continue;
        }
        let escape = chars
            .next()
            .ok_or(TableAdapterError::InvalidAst("truncated escape"))?;
        match escape {
            '"' | '\\' | '/' => result.push(escape),
            'b' => result.push('\u{0008}'),
            'f' => result.push('\u{000C}'),
            'n' => result.push('\n'),
            'r' => result.push('\r'),
            't' => result.push('\t'),
            'u' => {
                let first = hex_quad(&mut chars)?;
                let scalar = if (0xD800..=0xDBFF).contains(&first) {
                    if chars.next() != Some('\\') || chars.next() != Some('u') {
                        return Err(TableAdapterError::InvalidAst("missing low surrogate"));
                    }
                    let second = hex_quad(&mut chars)?;
                    if !(0xDC00..=0xDFFF).contains(&second) {
                        return Err(TableAdapterError::InvalidAst("invalid low surrogate"));
                    }
                    0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00)
                } else {
                    first
                };
                result.push(
                    char::from_u32(scalar)
                        .ok_or(TableAdapterError::InvalidAst("invalid Unicode escape"))?,
                );
            }
            _ => return Err(TableAdapterError::InvalidAst("unknown JSON escape")),
        }
    }
    Ok(result)
}

fn hex_quad(chars: &mut impl Iterator<Item = char>) -> Result<u32, TableAdapterError> {
    let mut value = 0_u32;
    for _ in 0..4 {
        let digit = chars
            .next()
            .and_then(|ch| ch.to_digit(16))
            .ok_or(TableAdapterError::InvalidAst("invalid Unicode escape"))?;
        value = value * 16 + digit;
    }
    Ok(value)
}

#[cfg(test)]
mod tests {
    use super::{TableAdapterError, json_string, quote};
    use crate::ast::{AstValue, AstValueKind, InputSpan};

    fn string_token(text: &str) -> AstValue {
        AstValue {
            kind: AstValueKind::Token,
            source_span: InputSpan {
                begin_byte: 0,
                end_byte: text.len() as u64,
            },
            recognized_span: InputSpan {
                begin_byte: 0,
                end_byte: text.len() as u64,
            },
            type_name: String::new(),
            variant_name: String::new(),
            token_kind: 0,
            token_text: text.to_owned(),
            field_names: Vec::new(),
            elements: Vec::new(),
        }
    }

    #[test]
    fn json_names_preserve_escapes_and_unicode() {
        assert_eq!(
            json_string(&string_token("\"E\\u004fF\"")),
            Ok("EOF".to_owned())
        );
        assert_eq!(
            json_string(&string_token("\"\\uD83D\\uDE80\"")),
            Ok("🚀".to_owned())
        );
        assert_eq!(quote("x\n\"\\"), "\"x\\n\\\"\\\\\"");
        assert_eq!(
            json_string(&string_token("\"\\uD83D\"")),
            Err(TableAdapterError::InvalidAst("missing low surrogate"))
        );
    }
}

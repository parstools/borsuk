//! Version-1 process/file boundary for a neutral AST and its exact source.

use crate::artifact::LoadedPackage;
use crate::ast::{AstValue, AstValueKind};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashSet;

pub const WIRE_VERSION: u32 = 1;
pub const MAXIMUM_WIRE_BYTES: usize = 128 << 20;
const MAXIMUM_NODES: usize = 1_000_000;
const MAXIMUM_DEPTH: usize = 200;
const MAXIMUM_JSON_DEPTH: usize = 2 * MAXIMUM_DEPTH + 8;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum AstWireError {
    Invalid(&'static str),
    Json(String),
}

#[derive(Clone, Copy)]
pub struct AstWireContext<'a> {
    pub ast_schema_version: u32,
    pub symbols_sha256: &'a str,
    pub ast_schema_sha256: &'a str,
    pub terminal_count: usize,
    pub source_name: &'a str,
    pub source: &'a [u8],
}

impl LoadedPackage {
    /// Binds a wire document to this package's symbol and AST schema catalogs.
    ///
    /// # Errors
    ///
    /// Returns an error if a required catalog descriptor is absent.
    pub fn wire_context<'a>(
        &'a self,
        source_name: &'a str,
        source: &'a [u8],
    ) -> Result<AstWireContext<'a>, AstWireError> {
        let descriptor = |kind| {
            self.manifest()
                .sections
                .iter()
                .find(|section| section.kind == kind)
                .map(|section| section.sha256.as_str())
                .ok_or(AstWireError::Invalid("missing catalog hash"))
        };
        Ok(AstWireContext {
            ast_schema_version: self.ast_schema().version,
            symbols_sha256: descriptor("symbols")?,
            ast_schema_sha256: descriptor("ast-schema")?,
            terminal_count: self.symbols().terminals.len(),
            source_name,
            source,
        })
    }
}

#[derive(Deserialize, Serialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct AstWireEnvelope {
    wire_version: u32,
    ast_schema_version: u32,
    symbols_sha256: String,
    ast_schema_sha256: String,
    source_name: String,
    source_byte_length: u64,
    source_sha256: String,
    root: AstValue,
}

fn source_hash(source: &[u8]) -> String {
    format!("{:x}", Sha256::digest(source))
}

fn valid_hash(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn validate_context(context: AstWireContext<'_>) -> Result<(), AstWireError> {
    if context.ast_schema_version != 1
        || context.source_name.is_empty()
        || context.terminal_count == 0
        || !valid_hash(context.symbols_sha256)
        || !valid_hash(context.ast_schema_sha256)
    {
        return Err(AstWireError::Invalid("invalid AST wire context"));
    }
    Ok(())
}

fn validate(
    value: &AstValue,
    source: &str,
    terminal_count: usize,
    depth: usize,
    nodes: &mut usize,
) -> Result<(), AstWireError> {
    *nodes = nodes
        .checked_add(1)
        .ok_or(AstWireError::Invalid("AST node count overflow"))?;
    if depth > MAXIMUM_DEPTH || *nodes > MAXIMUM_NODES {
        return Err(AstWireError::Invalid("AST resource limit exceeded"));
    }
    let source_length = source.len() as u64;
    let payload = value.source_span;
    let recognized = value.recognized_span;
    if payload.begin_byte > payload.end_byte
        || recognized.begin_byte > recognized.end_byte
        || payload.begin_byte < recognized.begin_byte
        || payload.end_byte > recognized.end_byte
        || recognized.end_byte > source_length
    {
        return Err(AstWireError::Invalid("invalid AST byte range"));
    }
    for offset in [
        payload.begin_byte,
        payload.end_byte,
        recognized.begin_byte,
        recognized.end_byte,
    ] {
        let index = usize::try_from(offset)
            .map_err(|_| AstWireError::Invalid("AST byte offset overflow"))?;
        if !source.is_char_boundary(index) {
            return Err(AstWireError::Invalid("AST range splits UTF-8"));
        }
    }
    let payload_begin = usize::try_from(payload.begin_byte)
        .map_err(|_| AstWireError::Invalid("AST byte offset overflow"))?;
    let payload_end = usize::try_from(payload.end_byte)
        .map_err(|_| AstWireError::Invalid("AST byte offset overflow"))?;
    match value.kind {
        AstValueKind::Unit => {
            if !value.type_name.is_empty()
                || !value.variant_name.is_empty()
                || value.token_kind != 0
                || !value.token_text.is_empty()
                || !value.field_names.is_empty()
                || !value.elements.is_empty()
            {
                return Err(AstWireError::Invalid("invalid unit AST value"));
            }
        }
        AstValueKind::Token => {
            if !value.type_name.is_empty()
                || !value.variant_name.is_empty()
                || !value.field_names.is_empty()
                || !value.elements.is_empty()
                || value.token_kind as usize >= terminal_count
                || source.get(payload_begin..payload_end) != Some(value.token_text.as_str())
            {
                return Err(AstWireError::Invalid("invalid token AST value"));
            }
        }
        AstValueKind::Node | AstValueKind::Record => {
            let mut names = HashSet::new();
            if value.type_name.is_empty()
                || value.field_names.len() != value.elements.len()
                || (value.kind == AstValueKind::Record && !value.variant_name.is_empty())
                || value
                    .field_names
                    .iter()
                    .any(|name| name.is_empty() || !names.insert(name.as_str()))
                || value.token_kind != 0
                || !value.token_text.is_empty()
            {
                return Err(AstWireError::Invalid("invalid node or record AST value"));
            }
        }
        AstValueKind::Optional | AstValueKind::List => {
            if !value.type_name.is_empty()
                || !value.variant_name.is_empty()
                || value.token_kind != 0
                || !value.token_text.is_empty()
                || !value.field_names.is_empty()
                || (value.kind == AstValueKind::Optional && value.elements.len() > 1)
            {
                return Err(AstWireError::Invalid("invalid optional or list AST value"));
            }
        }
    }
    for child in &value.elements {
        validate(child, source, terminal_count, depth + 1, nodes)?;
    }
    Ok(())
}

/// Emits canonical, versioned JSON for one accepted AST and its source identity.
///
/// # Errors
///
/// Rejects invalid schema/source metadata, malformed value shapes, and size limits.
pub fn emit_ast_wire(root: &AstValue, context: AstWireContext<'_>) -> Result<String, AstWireError> {
    validate_context(context)?;
    let text = std::str::from_utf8(context.source)
        .map_err(|_| AstWireError::Invalid("source is not UTF-8"))?;
    validate(root, text, context.terminal_count, 0, &mut 0)?;
    let envelope = AstWireEnvelope {
        wire_version: WIRE_VERSION,
        ast_schema_version: context.ast_schema_version,
        symbols_sha256: context.symbols_sha256.to_owned(),
        ast_schema_sha256: context.ast_schema_sha256.to_owned(),
        source_name: context.source_name.to_owned(),
        source_byte_length: context.source.len() as u64,
        source_sha256: source_hash(context.source),
        root: root.clone(),
    };
    let mut json = serde_json::to_string_pretty(&envelope)
        .map_err(|error| AstWireError::Json(error.to_string()))?;
    json.push('\n');
    if json.len() > MAXIMUM_WIRE_BYTES {
        return Err(AstWireError::Invalid("AST wire exceeds size limit"));
    }
    Ok(json)
}

/// Reads only the canonical version-1 wire format and verifies its source.
///
/// # Errors
///
/// Rejects version, hash, source, shape, range, or canonical-encoding mismatch.
pub fn parse_ast_wire(wire: &str, context: AstWireContext<'_>) -> Result<AstValue, AstWireError> {
    validate_context(context)?;
    if wire.len() > MAXIMUM_WIRE_BYTES {
        return Err(AstWireError::Invalid("AST wire exceeds size limit"));
    }
    // Bound nesting before recursive deserialization. JSON uses both an object
    // and an elements array per AST level, exceeding serde_json's default 128.
    let mut nesting = 0_usize;
    let mut in_string = false;
    let mut escaped = false;
    for byte in wire.bytes() {
        if in_string {
            if escaped {
                escaped = false;
            } else if byte == b'\\' {
                escaped = true;
            } else if byte == b'"' {
                in_string = false;
            }
        } else if byte == b'"' {
            in_string = true;
        } else if byte == b'{' || byte == b'[' {
            nesting += 1;
            if nesting > MAXIMUM_JSON_DEPTH {
                return Err(AstWireError::Invalid(
                    "AST wire JSON nesting exceeds the limit",
                ));
            }
        } else if byte == b'}' || byte == b']' {
            nesting = nesting
                .checked_sub(1)
                .ok_or(AstWireError::Invalid("AST wire JSON nesting is invalid"))?;
        }
    }
    let mut deserializer = serde_json::Deserializer::from_str(wire);
    deserializer.disable_recursion_limit();
    let envelope = AstWireEnvelope::deserialize(&mut deserializer)
        .map_err(|error| AstWireError::Json(error.to_string()))?;
    deserializer
        .end()
        .map_err(|error| AstWireError::Json(error.to_string()))?;
    if envelope.wire_version != WIRE_VERSION
        || envelope.ast_schema_version != context.ast_schema_version
        || envelope.symbols_sha256 != context.symbols_sha256
        || envelope.ast_schema_sha256 != context.ast_schema_sha256
        || envelope.source_name != context.source_name
        || envelope.source_byte_length != context.source.len() as u64
        || envelope.source_sha256 != source_hash(context.source)
    {
        return Err(AstWireError::Invalid("AST wire identity mismatch"));
    }
    let text = std::str::from_utf8(context.source)
        .map_err(|_| AstWireError::Invalid("source is not UTF-8"))?;
    validate(&envelope.root, text, context.terminal_count, 0, &mut 0)?;
    let mut canonical = serde_json::to_string_pretty(&envelope)
        .map_err(|error| AstWireError::Json(error.to_string()))?;
    canonical.push('\n');
    if canonical != wire {
        return Err(AstWireError::Invalid("noncanonical AST wire JSON"));
    }
    Ok(envelope.root)
}

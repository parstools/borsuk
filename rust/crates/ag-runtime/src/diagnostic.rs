//! Source locations and text rendering for parser and semantic diagnostics.

use std::path::Path;

use crate::ast::InputSpan;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Severity {
    Error,
    Warning,
}

impl Severity {
    const fn label(self) -> &'static str {
        match self {
            Self::Error => "error",
            Self::Warning => "warning",
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Diagnostic<'a> {
    pub severity: Severity,
    pub message: &'a str,
    pub source: InputSpan,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SourceLocation {
    pub line: usize,
    pub column: usize,
}

fn source_offset(source: &str, byte: u64) -> usize {
    let mut offset = usize::try_from(byte)
        .unwrap_or(usize::MAX)
        .min(source.len());
    while !source.is_char_boundary(offset) {
        offset -= 1;
    }
    offset
}

/// Returns the one-based position of a byte offset in UTF-8 source text.
#[must_use]
pub fn source_location(source: &str, byte: u64) -> SourceLocation {
    let offset = source_offset(source, byte);
    let prefix = &source[..offset];
    let line_start = prefix.rfind('\n').map_or(0, |newline| newline + 1);
    SourceLocation {
        line: prefix.bytes().filter(|byte| *byte == b'\n').count() + 1,
        column: prefix[line_start..].chars().count() + 1,
    }
}

/// Renders a diagnostic with its first source line and a caret underline.
/// The caller chooses the displayed path, for example relative to the current directory.
#[must_use]
pub fn render(path: &Path, source: &str, diagnostic: &Diagnostic<'_>) -> String {
    let begin = source_offset(source, diagnostic.source.begin_byte);
    let location = source_location(source, diagnostic.source.begin_byte);
    let line_start = source[..begin].rfind('\n').map_or(0, |newline| newline + 1);
    let raw_line_end = source[begin..]
        .find('\n')
        .map_or(source.len(), |relative| begin + relative);
    let line_end = if raw_line_end > line_start && source.as_bytes()[raw_line_end - 1] == b'\r' {
        raw_line_end - 1
    } else {
        raw_line_end
    };
    let line = &source[line_start..line_end];
    let marker_start = begin.min(line_end);
    let marker_end = source_offset(source, diagnostic.source.end_byte)
        .max(marker_start)
        .min(line_end);
    let prefix = &source[line_start..marker_start];
    let padding: String = prefix
        .chars()
        .map(|character| if character == '\t' { '\t' } else { ' ' })
        .collect();
    let width = source[marker_start..marker_end].chars().count().max(1);
    let gutter = location.line.to_string().len();
    format!(
        "{}: {}\n --> {}:{}:{}\n{:gutter$} |\n{} | {}\n{:gutter$} | {}{}",
        diagnostic.severity.label(),
        diagnostic.message,
        path.display(),
        location.line,
        location.column,
        "",
        location.line,
        line,
        "",
        padding,
        "^".repeat(width),
    )
}

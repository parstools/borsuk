use agas_runtime::ast::{AstValue, AstValueKind, InputSpan};
use agas_runtime::ast_wire::{AstWireContext, emit_ast_wire, parse_ast_wire};

fn context(source: &[u8]) -> AstWireContext<'_> {
    AstWireContext {
        ast_schema_version: 1,
        symbols_sha256: "0000000000000000000000000000000000000000000000000000000000000000",
        ast_schema_sha256: "1111111111111111111111111111111111111111111111111111111111111111",
        terminal_count: 1,
        source_name: "example.ag",
        source,
    }
}

fn forwarded_token() -> AstValue {
    AstValue {
        kind: AstValueKind::Token,
        source_span: InputSpan {
            begin_byte: 1,
            end_byte: 2,
        },
        recognized_span: InputSpan {
            begin_byte: 0,
            end_byte: 3,
        },
        type_name: String::new(),
        variant_name: String::new(),
        token_kind: 0,
        token_text: "x".to_owned(),
        field_names: Vec::new(),
        elements: Vec::new(),
    }
}

#[test]
fn round_trip_preserves_distinct_payload_and_recognized_ranges() {
    let source = b"(x)";
    let wire = emit_ast_wire(&forwarded_token(), context(source)).expect("canonical wire");
    assert_eq!(
        parse_ast_wire(&wire, context(source)),
        Ok(forwarded_token())
    );
    assert!(wire.contains("\"sourceSpan\": {"));
    assert!(wire.contains("\"recognizedSpan\": {"));
}

#[test]
fn wire_supports_depth_200_and_rejects_deeper_trees() {
    let source = b"(x)";
    let mut value = forwarded_token();
    for _ in 0..200 {
        value = AstValue {
            kind: AstValueKind::List,
            source_span: InputSpan {
                begin_byte: 0,
                end_byte: 3,
            },
            recognized_span: InputSpan {
                begin_byte: 0,
                end_byte: 3,
            },
            type_name: String::new(),
            variant_name: String::new(),
            token_kind: 0,
            token_text: String::new(),
            field_names: Vec::new(),
            elements: vec![value],
        };
    }
    let wire = emit_ast_wire(&value, context(source)).expect("depth 200 must emit");
    assert_eq!(parse_ast_wire(&wire, context(source)).unwrap(), value);
    let deeper = AstValue {
        elements: vec![value.clone()],
        ..value
    };
    assert!(emit_ast_wire(&deeper, context(source)).is_err());
    assert!(parse_ast_wire(&"[".repeat(1000), context(source)).is_err());
}

#[test]
fn rejects_invalid_token_catalog_text_ranges_and_fields() {
    let source = b"(x)";
    let mut value = forwarded_token();
    value.token_text = "y".to_owned();
    assert!(emit_ast_wire(&value, context(source)).is_err());
    value = forwarded_token();
    value.token_kind = 1;
    assert!(emit_ast_wire(&value, context(source)).is_err());
    value = forwarded_token();
    value.recognized_span.end_byte = 2;
    assert!(emit_ast_wire(&value, context(source)).is_ok());
    value.recognized_span.begin_byte = 2;
    assert!(emit_ast_wire(&value, context(source)).is_err());

    let wire = emit_ast_wire(&forwarded_token(), context(source)).expect("wire");
    assert!(
        parse_ast_wire(
            &wire.replace("\"tokenText\": \"x\"", "\"tokenText\": \"y\""),
            context(source)
        )
        .is_err()
    );
    assert!(
        parse_ast_wire(
            &wire.replace("\"kind\": \"token\"", "\"kind\": \"mystery\""),
            context(source)
        )
        .is_err()
    );
    assert!(
        parse_ast_wire(
            &wire.replace("\"kind\": \"token\"", "\"kind\": \"token\", \"extra\": 1"),
            context(source)
        )
        .is_err()
    );
    assert!(parse_ast_wire(&format!(" {wire}"), context(source)).is_err());
}

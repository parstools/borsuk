"""Template generation, stable identities, projection laws and protected writes."""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def wire_hash(value):
    return digest(json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode())


def main():
    parser = argparse.ArgumentParser()
    for name in ('sema', 'coge', 'repo'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    cases = 0

    def run(binary, *arguments, ok=True, code=None):
        nonlocal cases
        result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, text=True)
        assert (result.returncode == 0) == ok, result.stdout + result.stderr
        if code:
            assert code in result.stderr, result.stderr
        cases += 1
        return result

    def metadata(path, source):
        body = path.read_bytes()
        meta = json.loads(Path(str(path) + '.provenance.json').read_text())
        assert meta['projection'] == 'template'
        assert meta['source_sha256'] == digest(source)
        assert meta['output_sha256'] == digest(body)
        cursor = 0
        copied = bytearray()
        for fragment in meta['origins']:
            a, b = fragment['output_begin'], fragment['output_end']
            x, y = fragment['input_begin'], fragment['input_end']
            assert a == cursor and b > a
            cursor = b
            if not fragment['replacement']:
                assert body[a:b] == source[x:y]
                copied.extend(body[a:b])
        assert cursor == len(body)
        return meta, bytes(copied)

    with tempfile.TemporaryDirectory(prefix='agsem-templates-') as temporary:
        root = Path(temporary)
        grammar = root / 'input.ag'
        source = b'''// Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87: original UTF-8 bytes.\r\n'''+b'''grammar Template;
options { ast = explicit; }
lexerClasses { NORMAL = true; }
node root -> enable(NORMAL) : items=item* #Root | empty #Empty;
node item : id=ID #Name | 'z' #Literal;
inline duplicate : ID | ID;
ID : [a-y]+ -> require(NORMAL);
WS : [ \\t\\r\\n]+ -> skip;
'''
        grammar.write_bytes(source)
        sema = root / 'template.sema'
        run(args.sema, '--from-ag', grammar, '--emit-template', sema)
        meta, copied = metadata(sema, source)
        assert copied == source and meta['template_kind'] == 'sema' and meta['targets'] == []
        text = sema.read_text()
        status = re.compile(r'analysis_status\s*\{\s*id "([^"]+)";\s*syntax_sha256 "([^"]+)";\s*state pending;\s*\}')
        records = status.findall(text)
        assert len(records) == 6 and len({id for id, _ in records}) == 6
        expected = wire_hash(['agsem-alternative-v1', 'root', 'node', [['enable', 'NORMAL']],
                              False, 'Root', [['items', 'rule', 'item', None, 'zero_or_more']]])
        assert records[0][1] == expected
        prefix = 'alt-v1:' + wire_hash(['agsem-alt-id-v1', 'Template', expected]) + ':0'
        assert records[0][0] == prefix
        assert records[-2][1] == records[-1][1] and records[-2][0].endswith(':0') and records[-1][0].endswith(':1')
        again = root / 'again.sema'
        run(args.sema, '--from-ag', grammar, '--emit-template', again)
        assert again.read_bytes() == sema.read_bytes()
        projection = root / 'projected.ag'
        run(args.sema, '--emit-ag', projection, sema)
        assert 'analysis_status' not in projection.read_text() and 'analysis {' not in projection.read_text()
        reconstructed = root / 'reconstructed.sema'
        run(args.sema, '--from-ag', projection, '--emit-template', reconstructed)
        assert status.findall(reconstructed.read_text()) == records
        run(args.sema, '--check', sema, code='completeness.pending')
        rust = root / 'rust'
        run(args.sema, '--emit-rust-dir', rust, sema, ok=False, code='completeness.pending')
        assert not rust.exists()
        # Copying an identity and repeating a metadata block are explicit errors.
        duplicate = root / 'duplicate.sema'
        duplicate.write_text(text.replace(records[1][0], records[0][0]))
        run(args.sema, '--emit-ag', root / 'bad.ag', duplicate, ok=False, code='completeness.duplicate_alternative_id')
        block = status.search(text).group()
        duplicate.write_text(text.replace(block, block+' '+block, 1))
        run(args.sema, '--emit-ag', root / 'bad.ag', duplicate, ok=False, code='completeness.invalid_metadata')
        stale = root / 'stale.sema'
        stale.write_text(text.replace('items=item*', 'items=item+').replace('state pending;', 'state implemented;'))
        run(args.sema, '--check', stale, ok=False, code='completeness.stale_alternative')
        run(args.sema, '--emit-ag', root / 'stale.ag', stale)
        no_action = root / 'no-action.sema'
        no_action.write_text(text.replace('state pending;', 'state no_action;'))
        run(args.sema, '--emit-ag', root / 'bad.ag', no_action, ok=False)
        no_action.write_text(text.replace('state pending;', 'state no_action; reason "intentional";'))
        run(args.sema, '--check', no_action, ok=False, code='completeness.invalid_no_action')
        # All three paths work with incomplete analysis; existing analysis is copied verbatim.
        coge = root / 'template.coge'
        run(args.coge, '--from-ag', grammar, '--target', 'interpreter', '--emit-template', coge)
        meta, copied = metadata(coge, source)
        assert copied == source and meta['targets'] == ['interpreter']
        assert 'execute_handler' not in coge.read_text(), 'unknown IR must not invent variants'
        run(args.coge, '--check', coge, code='completeness.pending')
        exported = root / 'exported.sema'
        run(args.coge, '--emit-sema', exported, coge)
        assert status.findall(exported.read_text()) == records
        assert 'execution_obligations' not in exported.read_text()
        extended = root / 'extended.coge'
        run(args.coge, '--from-sema', sema, '--target', 'c', '--target', 'llvm', '--emit-template', extended)
        assert extended.read_bytes().startswith(sema.read_bytes().replace(b'sema', b'coge', 1))
        assert 'interpreter"' not in extended.read_text()
        run(args.coge, '--emit-sema', root / 'roundtrip.sema', extended)
        assert (root / 'roundtrip.sema').read_bytes() == sema.read_bytes()
        # A line comment at EOF can end in horizontal whitespace without a newline.
        basic = b"sema EndComment; grammar EndComment; node start : ID; ID:'a';"
        for index, ending in enumerate((b" // comment ", b" // comment\t", b" // comment",
                                        b" // comment \n", b" // comment \r\n", b" // comment \r")):
            source_bytes = basic + ending
            source_path = root / f'end-comment-{index}.sema'
            source_path.write_bytes(source_bytes)
            output_path = root / f'end-comment-{index}.coge'
            run(args.coge, '--from-sema', source_path, '--target', 'interpreter',
                '--emit-template', output_path)
            assert output_path.read_bytes().startswith(source_bytes.replace(b'sema', b'coge', 1))
            metadata(output_path, source_bytes)
            run(args.coge, '--check', output_path, code='completeness.pending')
            restored_path = root / f'end-comment-{index}-restored.sema'
            run(args.coge, '--emit-sema', restored_path, output_path)
            separator = b'' if ending.endswith((b'\n', b'\r')) else b'\n'
            assert restored_path.read_bytes() == source_bytes + separator
            projected = root / f'end-comment-{index}.ag'
            run(args.coge, '--emit-ag', projected, output_path)
            assert ending.strip() in projected.read_bytes()
            assert b'execution_obligations' not in projected.read_bytes()
        # Standalone real examples preserve all analysis bytes and resolve only explicit contracts.
        for language in ('toyc', 'toycp'):
            original = args.repo / f'generated/projections/{language}_typed.sema'
            contract = args.repo / f'contracts/{language}-runtime-v1.json'
            output = root / f'{language}.coge'
            run(args.coge, '--from-sema', original, '--contracts', contract,
                '--target', 'interpreter', '--emit-template', output)
            assert output.read_bytes().startswith(original.read_bytes().replace(b'sema', b'coge', 1))
            assert 'execute_handler"' in output.read_text() and 'evaluate_handler"' in output.read_text()
            restored = root / f'{language}.sema'
            run(args.coge, '--emit-sema', restored, '--contracts', contract, output)
            assert restored.read_bytes() == original.read_bytes()
            run(args.coge, '--emit-rust-dir', root / f'{language}-rust', '--contracts', contract,
                output, ok=False, code='completeness.pending')
            assert not (root / f'{language}-rust').exists()
        reordered = root / 'reordered.sema'
        match = re.search(r"inline duplicate\s*:\s*(.*?)\s*\|\s*(.*?)\s*;\s*(?=ID\s*:)", text, re.S)
        first, second = match.groups()
        reordered.write_text(text[:match.start(1)] + second + text[match.end(1):match.start(2)] +
                             first + text[match.end(2):])
        assert status.findall(reordered.read_text())[-2:] == records[-2:][::-1]
        run(args.sema, '--emit-ag', root / 'reordered.ag', reordered)
        # New words still work as ordinary Ag references and fields.
        keyword = root / 'keywords.ag'
        keyword.write_text("grammar Keywords; node start : analysis_status=execution_obligations; "
                           "inline execution_obligations : analysis_status; inline analysis_status : ID; ID:'a';")
        run(args.sema, '--from-ag', keyword, '--emit-template', root / 'keywords.sema')
        malformed = root / 'malformed.sema'
        malformed.write_text(text.replace('state pending;', 'state unknown;', 1))
        run(args.sema, '--emit-ag', root / 'bad.ag', malformed, ok=False, code='completeness.invalid_metadata')
        malformed.write_text(text.replace('id "'+records[0][0]+'";', 'id not_a_string;', 1))
        run(args.sema, '--emit-ag', root / 'bad.ag', malformed, ok=False, code='completeness.invalid_metadata')
        malformed_coge = root / 'malformed.coge'
        malformed_coge.write_text(coge.read_text().replace('targets "interpreter";', 'targets "unknown";'))
        run(args.coge, '--emit-ag', root / 'bad.ag', malformed_coge, ok=False, code='completeness.invalid_metadata')
        forbidden = root / 'forbidden.ag'
        forbidden.write_text("grammar Forbidden; options { generate_interpreter=true; } node start : ID; ID:'a';")
        run(args.sema, '--from-ag', forbidden, '--emit-template', root / 'forbidden.sema',
            ok=False, code='document.forbidden_option')
        assert not (root / 'forbidden.sema').exists()
        # Existing output, aliases and its manifest are protected before writing.
        before = sema.read_bytes()
        run(args.sema, '--from-ag', grammar, '--emit-template', sema, ok=False, code='output exists')
        assert sema.read_bytes() == before
        run(args.sema, '--from-ag', grammar, '--emit-template', sema, '--force')
        assert sema.read_bytes() == before
        run(args.sema, '--from-ag', grammar, '--emit-template', grammar, '--force', ok=False)
        alias = root / 'alias.ag'
        alias.symlink_to(grammar)
        run(args.sema, '--from-ag', grammar, '--emit-template', alias, '--force', ok=False)
        assert grammar.read_bytes() == source
        blocked = root / 'blocked.sema'
        Path(str(blocked)+'.provenance.json').symlink_to(grammar)
        run(args.sema, '--from-ag', grammar, '--emit-template', blocked, '--force', ok=False)
        assert not blocked.exists()
        run(args.coge, '--from-ag', grammar, '--emit-template', root / 'missing.coge', ok=False, code='--target')
        run(args.coge, '--from-ag', grammar, '--target', 'c', '--target', 'c', '--emit-template', root / 'bad.coge', ok=False)
        run(args.coge, '--from-ag', grammar, '--target', 'unknown', '--emit-template', root / 'bad.coge', ok=False)
        run(args.sema, '--from-sema', sema, '--emit-template', root / 'bad.sema', ok=False)
        run(args.coge, '--from-sema', coge, '--target', 'c', '--emit-template', root / 'bad.coge', ok=False, code='document.wrong_kind')
        run(args.sema, '--emit-template', root / 'bad.sema', grammar, ok=False)
        run(args.sema, '--from-ag', grammar, '--check', ok=False)
        invalid = root / 'invalid.ag'
        invalid.write_text('grammar Broken; node start : missing;')
        output = root / 'invalid.sema'
        run(args.sema, '--from-ag', invalid, '--emit-template', output, ok=False, code='template.invalid_ag')
        assert not output.exists() and not Path(str(output)+'.provenance.json').exists()
    print(f'{cases} template CLI cases passed')


if __name__ == '__main__':
    main()

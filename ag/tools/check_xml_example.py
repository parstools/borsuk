#!/usr/bin/env python3
"""Check the standalone XML grammar, preserved data and optional runtime parity."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def walk(value):
    yield value
    for child in value['elements']:
        yield from walk(child)


def field(value, name):
    return value['elements'][value['fieldNames'].index(name)]


def matching_names(root):
    # Name equality is checked after parsing; lexer classes only select tokens.
    for tag in (value for value in walk(root) if value['typeName'] == 'tag'):
        ending = next(value for value in walk(field(tag, 'tail'))
                      if value['typeName'] == 'ending')
        if ending['variantName'] == 'Paired':
            if field(tag, 'name')['tokenText'] != field(ending, 'close')['tokenText']:
                return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agas', type=Path, required=True)
    parser.add_argument('--grammar', type=Path, required=True)
    parser.add_argument('--example', type=Path, required=True)
    parser.add_argument('--ast-wire', type=Path, help='Rust AST wire binary for parity')
    args = parser.parse_args()
    parsers = [[str(args.agas.resolve()), '--parse-package']]
    if args.ast_wire:
        parsers.append([str(args.ast_wire.resolve())])
    with tempfile.TemporaryDirectory(prefix='agas-xml-') as temporary:
        temporary = Path(temporary)
        package = temporary / 'package'
        emitted = subprocess.run(
            [str(args.agas.resolve()), '--emit-package', str(package),
             str(args.grammar.resolve())], capture_output=True, text=True)
        assert emitted.returncode == 0, emitted.stdout + emitted.stderr
        assert json.loads((package / 'lexer.json').read_text())['version'] == 2
        source = temporary / 'input.xml'
        checked = 0

        def check(text, accepted=True, names=True):
            nonlocal checked
            source.write_text(text, encoding='utf-8')
            data = source.read_bytes()
            outputs = []
            for command in parsers:
                result = subprocess.run(command + [str(package), str(source)],
                                        capture_output=True, text=True)
                assert (result.returncode == 0) == accepted, (text, result.stderr)
                if accepted:
                    wire = json.loads(result.stdout)
                    assert wire['sourceByteLength'] == len(data)
                    assert wire['sourceSha256'] == hashlib.sha256(data).hexdigest()
                    assert matching_names(wire['root']) == names, text
                    for token in (value for value in walk(wire['root'])
                                  if value['kind'] == 'token'):
                        span = token['sourceSpan']
                        assert data[span['beginByte']:span['endByte']] == token['tokenText'].encode('utf-8')
                    outputs.append(wire)
                else:
                    assert any(message in result.stderr for message in (
                        'source has a syntax error', 'cannot form a token',
                        'NoMatchingRule')), result.stderr
            if len(outputs) == 2:
                assert outputs[0] == outputs[1], 'C++/Rust AST wire differs'
            checked += 1
            return outputs[0]['root'] if outputs else None

        check(args.example.read_text(encoding='utf-8'))
        for text in (
            '<root/>', '<root />', '<root></root>', ' \n<root/>\t',
            '<root><child/><child>value</child></root>',
            '<ns:root attr.name="x" ns:a=\'y\'/>',
            '<r empty="" single=\'\' spaces = " a>b \' " other=\' " > \'/>',
            '<?xml version="1.0"?><r/>',
            ' \n<?xml version=\'1.0\' encoding="UTF-8" ?>\n<r/>\n',
            '<r><same><same/></same>tail<other /></r>',
            '<r> > " \' / = </r>', '<r a="value"><child/>after</r>',
        ):
            check(text)
        text_data = ' \n żółć 🙂 > \' " / = \t '
        root = check('<r>' + text_data + '</r>')
        assert any(value['kind'] == 'token' and value['tokenText'] == text_data
                   for value in walk(root)), 'content whitespace or Unicode was lost'
        for text in (
            '', '<r>', '<r/>tail', '<r/><s/>', '<r a=unquoted/>',
            '<r a="unfinished', "<r a='unfinished", '<r a="<bad"/>',
            '<r a="x"b="y"/>', '<r a="x"/> </r>',
            '<r><child></r>', '<r></r', '<r>&amp;</r>',
            '<!-- unsupported --> <r/>', '<r><![CDATA[unsupported]]></r>',
        ):
            check(text, accepted=False)
        check('<open></close>', names=False)
        check('<r><a></b></r>', names=False)
        print(f'XML: {checked} cases; text/UTF-8 spans and post-parse names checked; '
              f'{len(parsers)} runtime(s)')


if __name__ == '__main__':
    main()

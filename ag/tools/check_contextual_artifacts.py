#!/usr/bin/env python3
"""Exercise contextual packages through independent C++ and Rust runtimes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--agas', type=Path, default=root / 'build/bin/agas')
    cli.add_argument('--ast-wire', type=Path, default=root / 'rust/target/debug/agas_ast_wire')
    args = cli.parse_args()
    fixtures = root / 'ag/grammars/contextual-lexer'
    cases = {
        'Shift': [('a>>b', True), ('c>>d', True), ('a > > b', False), ('a>>', False)],
        'GatedBrackets': [('a>>b', True), ('c > > d', True), ('a > > b', False)],
        'Keywords': [('@read;', True), ('@write;', True), ('#reader;', True),
                     ('#write;', True), ('@reader;', False), ('@read', False)],
        'Combined': [('0read>>;', True), ('1write>>;', True), ('2read>>;', True),
                     ('3read>>;', True), ('3write>>;', True), ('3reader>>;', False),
                     ('2read > >;', False), ('0read>>;3read>>;', False)],
        'Nested': [('read outer<read<int>>>>write;', True),
                   ('read outer<read<int> > >> write;', True),
                   ('read outer<read<int>> > > write;', False)],
    }
    checked = 0
    with tempfile.TemporaryDirectory(prefix='agas-context-artifacts-') as temporary:
        temporary = Path(temporary)
        source = temporary / 'input.vs'

        def run(package, text, accepted):
            nonlocal checked
            source.write_text(text)
            outputs = []
            for command in ([str(args.agas.resolve()), '--parse-package'], [str(args.ast_wire.resolve())]):
                result = subprocess.run(command + [str(package), str(source)], capture_output=True, text=True)
                if (result.returncode == 0) != accepted:
                    raise AssertionError(f'{package.name}: {text!r}: {command}: {result.stderr}')
                if accepted:
                    outputs.append(json.loads(result.stdout))
                elif not any(message in result.stderr for message in
                             ('source has a syntax error', 'cannot form a token', 'NoMatchingRule')):
                    raise AssertionError(f'unexpected failure: {result.stderr}')
            if accepted and outputs[0] != outputs[1]:
                raise AssertionError(f'{package.name}: C++/Rust AST wire differs for {text!r}')
            checked += 1

        for name, samples in cases.items():
            for algorithm in ('LR', 'LALR'):
                if name == 'Nested' and algorithm == 'LALR':
                    continue  # This fixture intentionally needs canonical contexts.
                for k in (1, 2):
                    grammar = temporary / f'{name}-{algorithm}-{k}.ag'
                    text = (fixtures / f'{name}.ag').read_text()
                    text = text.replace('parser = LALR', f'parser = {algorithm}').replace('lookahead = 2', f'lookahead = {k}')
                    text = text.replace('lexerClasses', 'channels { HIDDEN }\nlexerClasses', 1)
                    # Keep trivia and exercise prioritized/non-greedy matching.
                    text += "\nCOMMENT : '/*' .*? '*/' -> channel(HIDDEN);\n"
                    grammar.write_text(text)
                    package = grammar.with_suffix('.package')
                    subprocess.run([str(args.agas.resolve()), '--emit-package', str(package), str(grammar)],
                                   stdout=subprocess.DEVNULL, check=True)
                    for sample, accepted in samples:
                        run(package, sample, accepted)
                        run(package, ' /* żółć 🙂 */ ' + sample + ' /* end */ ', accepted)

        grammar = temporary / 'High.ag'
        classes = ' '.join(f'C{i} = false;' for i in range(64))
        grammar.write_text('grammar High; lexerClasses { ' + classes +
                           " } node start -> enable(C63) : value=A; A : 'a' -> require(C63);")
        package = temporary / 'high-package'
        subprocess.run([str(args.agas.resolve()), '--emit-package', str(package), str(grammar)],
                       stdout=subprocess.DEVNULL, check=True)
        run(package, 'a', True)
        run(package, '', False)
        original = json.loads((package / 'lexer.json').read_text())
        original_manifest = json.loads((package / 'manifest.json').read_text())
        mutations = [
            lambda ctx: ctx['requiredClasses'].clear(),
            lambda ctx: ctx['originalTerminals'].__setitem__(0, 1000000),
            lambda ctx: ctx['rows'].pop(),
            lambda ctx: ctx['rows'][0][0]['edges'][0].__setitem__('target', 0),
        ]
        for mutate in mutations:
            lexer = json.loads(json.dumps(original))
            mutate(lexer['context'])
            data = json.dumps(lexer).encode()
            (package / 'lexer.json').write_bytes(data)
            manifest = json.loads(json.dumps(original_manifest))
            descriptor = next(section for section in manifest['sections'] if section['kind'] == 'lexer')
            descriptor.update(byteLength=len(data), sha256=hashlib.sha256(data).hexdigest())
            (package / 'manifest.json').write_text(json.dumps(manifest))
            source.write_text('a')
            for command in ([str(args.agas.resolve()), '--parse-package'], [str(args.ast_wire.resolve())]):
                result = subprocess.run(command + [str(package), str(source)], capture_output=True, text=True)
                if result.returncode == 0 or 'source has a syntax error' in result.stderr:
                    raise AssertionError('malformed context was not rejected during package validation')
        print(f'Contextual artifacts: {checked} C++/Rust parity cases; 4 malformed packages rejected by both loaders.')


if __name__ == '__main__':
    main()

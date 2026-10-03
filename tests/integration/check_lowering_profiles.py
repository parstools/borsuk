#!/usr/bin/env python3
"""Check typed profile expansion, overrides and unchanged generated artifacts."""

import argparse
import copy
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--coge', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--contracts', type=Path, required=True)
    args = parser.parse_args()
    binary = str(args.coge.resolve())
    authored = args.source.read_text()
    manifest = json.loads(args.contracts.read_text())
    legacy = args.source.with_suffix('.sema').read_text()
    begin = authored.index('lowering_model {\n')
    end = authored.index('execution_model {\n', begin)
    legacy_begin = legacy.index('lowering_model {\n')
    legacy_end = legacy.index('execution_model {\n', legacy_begin)
    explicit = authored[:begin] + legacy[legacy_begin:legacy_end] + authored[end:]

    with tempfile.TemporaryDirectory(prefix='coge-profiles-') as temporary:
        root = Path(temporary)

        def run(name, source=authored, contracts=manifest, operation='--check', error=None):
            path = root / (name + '.coge')
            contract = root / (name + '.json')
            path.write_text(source)
            contract.write_text(json.dumps(contracts))
            output = root / (name + '-out')
            command = [binary, operation]
            if operation != '--check':
                command.append(str(output))
            command += ['--contracts', str(contract), str(path)]
            result = subprocess.run(command, capture_output=True, text=True)
            if error:
                assert result.returncode != 0 and error in result.stderr, (name, result.stdout, result.stderr)
                assert not output.exists(), f'{name}: invalid input wrote output'
            else:
                assert result.returncode == 0, (name, result.stdout, result.stderr)
            return output

        profile_output = run('profile', operation='--emit-rust-dir')
        explicit_output = run('explicit', source=explicit, operation='--emit-rust-dir')
        generated = sorted(path.name for path in profile_output.glob('*.rs'))
        assert generated == sorted(path.name for path in explicit_output.glob('*.rs'))
        for name in generated:
            assert (profile_output / name).read_bytes() == (explicit_output / name).read_bytes(), name

        inspected = run('inspect', operation='--inspect-model')
        repeated = run('repeat', operation='--inspect-model')
        model = json.loads(inspected.read_text())
        expansion = model['execution_configuration']
        assert expansion == json.loads(repeated.read_text())['execution_configuration']
        lowering = expansion['lowering']
        assert lowering['profile']['id'] == 'structured_core_v1'
        assert len(lowering['profile']['sha256']) == 64
        roles = {entry['role']: entry for entry in lowering['bindings']}
        assert len(roles) == 43
        assert roles['lower']['origin'] == 'explicit'
        assert roles['return_ir']['origin'] == 'profile_default'
        assert roles['return_ir']['target'] == 'Return'
        for entry in lowering['bindings']:
            assert authored[entry['begin_byte']:entry['end_byte']].strip()
        checks = {entry['role']: entry for entry in lowering['type_checks'] if entry['role']}
        assert checks['function_body']['type']['name'] == 'Option'
        assert checks['return_ir']['payload'][0]['name'] == 'Option'
        assert checks['binary_ir']['payload'][0]['name'] == 'Operator'
        assert lowering['interface']['effect'] == 'pure'
        for backend, profile in [('backend_c', 'core_c_v1'), ('backend_llvm', 'core_llvm_v1')]:
            entry = expansion[backend]
            assert entry['profile']['id'] == profile
            assert len(entry['profile']['sha256']) == 64
            assert entry['interface']['effect'] == 'pure'
            assert entry['bindings'][1]['origin'] == 'checked_lowering'
            assert entry['bindings'][1]['target'] == 'lower_function_to_structured'
        explicit_model = json.loads(run('explicit-inspect', source=explicit, operation='--inspect-model').read_text())
        assert explicit_model['execution_configuration']['lowering']['profile'] is None

        def change_type(owner, update):
            changed = copy.deepcopy(manifest)
            entry = next(item for item in changed['semantic'] if item['name'] == owner)
            update(entry)
            return changed

        # A same-typed extra field never changes the explicitly selected role.
        extra = change_type('Context', lambda item: item['fields'].update({'other_functions': copy.deepcopy(item['fields']['functions'])}))
        extra_model = json.loads(run('same-type', contracts=extra, operation='--inspect-model').read_text())
        assert extra_model['execution_configuration']['lowering']['bindings'] == lowering['bindings']
        override = authored.replace('profile = structured_core_v1;', 'profile = structured_core_v1;\n    functions = other_functions;')
        override_model = json.loads(run('override', source=override, contracts=extra, operation='--inspect-model').read_text())
        override_expansion = override_model['execution_configuration']['lowering']
        assert override_expansion['profile'] == lowering['profile']
        function_role = next(entry for entry in override_expansion['bindings'] if entry['role'] == 'functions')
        assert function_role['target'] == 'other_functions' and function_role['origin'] == 'explicit'
        override_rust = run('override-rust', source=override, contracts=extra, operation='--emit-rust-dir')
        assert b'ctx.other_functions' in (override_rust / 'lowering_gen.rs').read_bytes()
        run('bad-override', source=override, error='lowering field type mismatch: Context.other_functions')
        wrong = change_type('Context', lambda item: item['fields'].update({'functions': {'name': 'List', 'arguments': [{'name': 'Scope'}]}}))
        run('wrong-field', contracts=wrong, error='requires Context.functions:', operation='--emit-rust-dir')
        wrong = change_type('Operation', lambda item: item['variants'].update({'Return': [{'name': 'ExprId'}]}))
        run('wrong-payload', contracts=wrong, error='variant payload mismatch Operation.Return')
        wrong = change_type('Operator', lambda item: item['variants'].update({'Add': [{'name': 'I32'}]}))
        run('operator-payload', contracts=wrong, error='variant payload mismatch Operator.Add')
        wrong = change_type('ExpressionKind', lambda item: item['variants'].pop('IndexLoad'))
        run('missing-variant', contracts=wrong, error='lowering variant payload mismatch: ExpressionKind.IndexLoad')
        run('unknown-profile', source=authored.replace('structured_core_v1', 'unknown_core'), error='unknown lowering profile: unknown_core')
        run('missing-lower', source=authored.replace('    lower = lower_function_to_structured;\n', ''), error='lowering_model missing field lower')
        run('duplicate', source=authored.replace('profile = structured_core_v1;', 'profile = structured_core_v1;\n    profile = structured_core_v1;'), error='lowering_model duplicate field profile')
        run('helper', source=authored.replace('lower_function_to_structured', 'reachable_functions'), error='lowering_model function name conflicts with helper')
        run('wrong-backend', source=authored.replace('core_c_v1', 'core_llvm_v1'), error='unknown backend profile: core_llvm_v1')
        run('backend-mismatch', source=authored.replace('profile = core_c_v1;', 'profile = core_c_v1;\n    lower = another_lower;'), error='backend_c lower differs from lowering_model')
        renamed = authored.replace('lower_function_to_structured', 'lower_custom').replace('profile = core_c_v1;', 'profile = core_c_v1;\n    emit = emit_custom;')
        renamed_output = run('renamed', source=renamed, operation='--emit-rust-dir')
        assert b'pub fn emit_custom(' in (renamed_output / 'backend_c_gen.rs').read_bytes()
        assert b'crate::lowering_gen::lower_custom' in (renamed_output / 'backend_llvm_gen.rs').read_bytes()
        # ToyCP is not silently assigned the incompatible scalar/array IR profile.
        toycp = args.source.with_name('toycp_typed.coge').read_text()
        toycp_manifest = json.loads(args.contracts.with_name('toycp-runtime-v1.json').read_text())
        toycp = toycp.replace('execution_model {\n', authored[begin:end] + 'execution_model {\n', 1)
        run('toycp', source=toycp, contracts=toycp_manifest, error='lowering variant payload mismatch: Operation.IndexStore')


if __name__ == '__main__':
    main()

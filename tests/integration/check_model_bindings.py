#!/usr/bin/env python3
"""Verify typed semantic schemas against explicit adapters and invalid contracts."""
import argparse
import copy
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from coge_cli import explicit_model_bindings


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--coge', type=Path, required=True)
    parser.add_argument('--sema', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    sources = {name: (args.root / f'{name}_typed.coge').read_text() for name in ['toyc', 'toycp']}
    contracts = {name: json.loads((args.root / f'contracts/{name}-runtime-v1.json').read_text()) for name in sources}
    with tempfile.TemporaryDirectory(prefix='agsem-bindings-') as temporary:
        root = Path(temporary)
        def run(name, language='toyc', source=None, manifest=None, operation='--check', error=None, binary=None):
            source = sources[language] if source is None else source
            manifest = contracts[language] if manifest is None else manifest
            path = root / (name + '.coge')
            contract = root / (name + '.json')
            path.write_text(source)
            contract.write_text(json.dumps(manifest))
            output = root / (name + '-out')
            command = [str((binary or args.coge).resolve()), operation]
            if operation != '--check': command.append(str(output))
            result = subprocess.run(command + ['--contracts', str(contract), str(path)], text=True, capture_output=True)
            if error:
                assert result.returncode != 0 and error in result.stderr, (name, result.stdout, result.stderr)
                assert not output.exists(), f'{name}: invalid input wrote files'
            else: assert result.returncode == 0, (name, result.stdout, result.stderr)
            return output
        def inspect(name, **kwargs):
            return json.loads(run(name, operation='--inspect-model', **kwargs).read_text())
        def change(owner, update, language='toyc'):
            result = copy.deepcopy(contracts[language])
            symbol = next(value for value in result['semantic'] if value['name'] == owner)
            update(symbol)
            return result
        def resolved(model):
            ports = copy.deepcopy(model['model_bindings'])
            for port in ports:
                for key in ['source', 'policy_source', 'origin']: port.pop(key)
            return ports, model['binding_dependencies']
        models = {}
        for language, source in sources.items():
            models[language] = inspect(language, language=language)
            repeat = inspect(language + '-repeat', language=language)
            assert models[language] == repeat
            model = models[language]
            assert model['format'] == 'agsem-checked-semantic-expansion-v3'
            assert model['binding_schema']['id'] == 'standard_semantic_v1'
            assert len(model['binding_schema']['sha256']) == 64
            assert len(model['model_bindings']) == (30 if language == 'toyc' else 56)
            assert model['binding_representation_obligations']
            for port in model['model_bindings']:
                assert port['origin'] in ['explicit', 'schema_default']
                assert source[port['source']['begin_byte']:port['source']['end_byte']].strip()
                for target in port['targets']:
                    assert 'declaration' not in target
                    assert target['contract']['id'] == language + '-runtime'
                    assert target['contract']['path'].startswith('/semantic/')
            explicit = explicit_model_bindings(args.root / f'{language}_typed.coge')
            before = inspect(language + '-explicit', language=language, source=explicit)
            assert before['binding_schema'] is None
            assert resolved(before) == resolved(model)
            generated = run(language + '-generated', language=language, operation='--emit-rust-dir')
            metadata = json.loads((generated / 'sema_generation.provenance.json').read_text())
            assert metadata['binding_schema']['sha256'] == model['binding_schema']['sha256']
            explicit_generated = run(language + '-explicit-generated', language=language, source=explicit, operation='--emit-rust-dir')
            assert sorted(p.name for p in generated.glob('*.rs')) == sorted(p.name for p in explicit_generated.glob('*.rs'))
            for path in generated.glob('*.rs'):
                assert path.read_bytes() == (explicit_generated / path.name).read_bytes(), path.name
                assert path.read_bytes() == (args.root / f'examples/{language}_typed/generated' / path.name).read_bytes(), path.name
            projected = run(language + '-projection', language=language, operation='--emit-sema')
            assert 'model_schema standard_semantic_v1;' in projected.read_text()
            assert 'assignment_bindings' not in projected.read_text()
            projection_model = inspect(language + '-projected', language=language, source=projected.read_text(), binary=args.sema)
            assert resolved(projection_model) == resolved(model)
            assert projection_model['binding_schema']['sha256'] == model['binding_schema']['sha256']
            provenance = json.loads(Path(str(projected) + '.provenance.json').read_text())
            assert provenance['binding_schema']['sha256'] == model['binding_schema']['sha256']
        ports = {(p['family'], p['port']): p for p in models['toyc']['model_bindings']}
        assert ports['condition', 'emit']['targets'][0]['required_signature']['result']['name'] == 'ExprId'
        assert ports['return', 'emit']['targets'][0]['required_signature']['result']['name'] == 'OpId'
        assert ports['assignment', 'mark']['targets'][0]['actual_signature']['effect'] == 'pure'
        assert ports['assignment', 'mark']['argument_passing'][0] == 'borrow_mut'
        schema_line = '    model_schema standard_semantic_v1;\n'
        run('unknown', source=sources['toyc'].replace('standard_semantic_v1', 'unknown_v1'), error='sema.unknown_model_schema')
        run('duplicate-schema', source=sources['toyc'].replace(schema_line, schema_line * 2), error='sema.duplicate_model_schema')
        block = re.search(r'    model_bindings function_return \{\n.*?    \}\n', sources['toyc'], re.DOTALL).group()
        run('duplicate-block', source=sources['toyc'].replace(block, block * 2), error='sema.duplicate_binding')
        run('duplicate-key', source=sources['toyc'].replace('        return_ir = tuple;', '        return_ir = tuple;\n        return_ir = tuple;'), error='sema.duplicate_binding')
        run('orphan', source=sources['toyc'].replace('model_bindings function_return', 'model_bindings wrong_policy'), error='sema.orphan_binding')
        run('unknown-port', source=sources['toyc'].replace('        return_ir = tuple;', '        return_ir = tuple;\n        unknown = functions;'), error='sema.unknown_binding_slot')
        for key in ['return_ir = tuple', 'cleanup_scopes = no_scopes', 'error_ir = Error']:
            run('missing-' + key.split()[0], source=sources['toyc'].replace('        ' + key + ';\n', ''), error='sema.missing_binding')
        run('missing-construction', language='toycp', source=sources['toycp'].replace('        construction_ir = Construct;\n', ''), error='sema.missing_binding')
        run('no-schema', source=sources['toyc'].replace(schema_line, ''), error='sema.missing_binding')
        run('unused-cleanup', source=sources['toyc'].replace('        return_ir = tuple;', '        return_ir = tuple;\n        scopes = scopes;'), error='sema.unknown_binding_slot')
        run('cleanup-conflict', source=sources['toyc'].replace('cleanup_scopes = no_scopes', 'cleanup_scopes = active_scopes'), error='cleanup and cleanup_scopes disagree')
        run('cleanup-tuple', language='toycp', source=sources['toycp'].replace('return_ir = record', 'return_ir = tuple'), error='cleanup requires record')
        # Local field and method overrides select only the specified family's ports.
        custom = copy.deepcopy(contracts['toyc'])
        context = next(item for item in custom['semantic'] if item['name'] == 'Context')
        context['fields']['branch_flow'] = {'name': 'Flow'}
        predicate = copy.deepcopy(next(item for item in custom['semantic'] if item['name'] == 'poisoned_expression'))
        predicate['name'] = 'branch_poisoned'
        custom['semantic'].append(predicate)
        override = sources['toyc'].replace(schema_line, schema_line + '    assignment_bindings store_value { flow = branch_flow; poison = branch_poisoned; }\n')
        changed = inspect('override', source=override, manifest=custom)
        for port in changed['model_bindings']:
            if port['family'] == 'assignment' and port['port'] in ['flow', 'poison']:
                assert port['origin'] == 'explicit'
                assert port['value'] in ['branch_flow', 'branch_poisoned']
            elif port['port'] == 'flow': assert port['value'] == 'current_flow'
        emitted = run('override-emitted', source=override, manifest=custom, operation='--emit-rust-dir')
        library = (emitted / 'sema_lib_gen.rs').read_text()
        assert 'self.branch_flow' in library and 'self.branch_poisoned(value)' in library
        assert changed['binding_schema']['sha256'] == models['toyc']['binding_schema']['sha256']
        # Same-typed alternatives cannot rescue a missing default target.
        del context['fields']['current_flow']
        run('no-fallback', manifest=custom, error='sema.binding_type_mismatch')
        for owner, field, bad in [('Context', 'functions', {'name': 'List', 'arguments': [{'name': 'Expression'}]}), ('Flow', 'reachable', {'name': 'Int'}), ('Function', 'result', {'name': 'Bool'})]:
            wrong = change(owner, lambda item: item['fields'].update({field: bad}))
            run('bad-' + owner + '-' + field, manifest=wrong, error='sema.binding_', operation='--emit-rust-dir')
        for key, bad, code in [('parameters', [{'name': 'Type'}], 'sema.binding_type_mismatch'), ('result', {'name': 'Int'}, 'sema.binding_type_mismatch'), ('effect', 'mutates', 'sema.binding_effect_mismatch'), ('context', 'Flow', 'sema.binding_target_mismatch'), ('kind', 'function', 'sema.binding_target_mismatch'), ('rust', ['Other', 'predicate'], 'sema.binding_target_mismatch')]:
            wrong = change('poisoned_expression', lambda item: item.update({key: bad}))
            run('method-' + key, manifest=wrong, error=code)
        wrong = copy.deepcopy(contracts['toyc'])
        external = next(item for item in wrong['semantic'] if item['name'] == 'poisoned_expression')
        wrong['semantic'].remove(external)
        wrong.setdefault('execution', []).append(external)
        run('execution-method', manifest=wrong, error='sema.binding_target_mismatch')
        missing = copy.deepcopy(contracts['toyc'])
        missing['semantic'] = [item for item in missing['semantic'] if item['name'] != 'poisoned_expression']
        run('missing-method', manifest=missing, error='sema.binding_target_mismatch')
        # A second Type -> Bool predicate does not replace the destruction role.
        wrong = copy.deepcopy(contracts['toycp'])
        predicate = next(item for item in wrong['semantic'] if item['name'] == 'type_needs_destruction')
        alternative = copy.deepcopy(predicate)
        alternative['name'] = 'other_destruction_predicate'
        wrong['semantic'].remove(predicate)
        wrong['semantic'].append(alternative)
        run('no-method-fallback', language='toycp', manifest=wrong, error='sema.binding_target_mismatch')
        custom_return = sources['toycp'].replace('cleanup_scopes = active_scopes;', 'cleanup_scopes = active_scopes;\n        destructible = other_destruction_predicate;')
        selected = inspect('destruction-override', language='toycp', source=custom_return, manifest=wrong)
        assert next(port for port in selected['model_bindings'] if port['family'] == 'return' and port['port'] == 'destructible')['value'] == 'other_destruction_predicate'
        wrong = change('Operation', lambda item: item['variants'].update({'Error': [{'name': 'ExprId'}]}))
        run('error-payload', manifest=wrong, error='sema.binding_type_mismatch')
        wrong = change('Operation', lambda item: item['variants'].update({'Return': [{'name': 'ExprId'}]}))
        run('return-payload', manifest=wrong, error='sema.binding_dependency_mismatch')
        wrong = change('Operation', lambda item: item['variants'].update({'CompoundStore': [{'name': 'ExprId'}]}), language='toycp')
        run('compound-payload', language='toycp', manifest=wrong, error='sema.binding_dependency_mismatch')
        for owner, field in [('Context', 'fields'), ('Struct', 'fields')]:
            wrong = change(owner, lambda item: item['fields'].update({field: {'name': 'List', 'arguments': [{'name': 'ExprId'}]}}), language='toycp')
            run('selection-' + owner, language='toycp', manifest=wrong, error='sema.binding_type_mismatch')
        wrong = change('Variable', lambda item: item['fields'].pop('is_self'), language='toycp')
        run('cleanup-self', language='toycp', manifest=wrong, error='sema.binding_dependency_mismatch')
        wrong = copy.deepcopy(contracts['toycp'])
        wrong['semantic'] = [item for item in wrong['semantic'] if item['name'] != 'type_needs_destruction']
        run('cleanup-method', language='toycp', manifest=wrong, error='sema.binding_target_mismatch')
        # An unused cleanup or selection dependency is not imposed on ToyC.
        reduced = copy.deepcopy(contracts['toyc'])
        context = next(item for item in reduced['semantic'] if item['name'] == 'Context')
        context['fields'].pop('active_scopes', None)
        variable = next(item for item in reduced['semantic'] if item['name'] == 'Variable')
        variable['fields'].pop('is_self', None)
        reduced['semantic'] = [item for item in reduced['semantic'] if item['name'] not in ['type_needs_destruction', 'accessible', 'conversion_allowed', 'default_constructible']]
        run('conditional-dependencies', manifest=reduced)
        # Extension keywords remain legal Ag rule names outside MODEL.
        ag_name = 'sema Names; grammar Names; node model_schema : ID; ID : \'x\';'
        run('ag-rule-name', source=ag_name, manifest={'format': 1, 'id': 'empty', 'version': '1'}, binary=args.sema)

if __name__ == '__main__': main()

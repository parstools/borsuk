#!/usr/bin/env python3
"""Verify execution profile expansion against independently authored models."""
import argparse
import copy
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path


def closing(text, start):
    depth = 0
    quoted = False
    escaped = False
    comment = False
    for i in range(start, len(text)):
        c = text[i]
        if comment:
            if c == '\n': comment = False
            continue
        if quoted:
            if escaped: escaped = False
            elif c == '\\': escaped = True
            elif c == '"': quoted = False
            continue
        if c == '"': quoted = True
        elif c == '/' and text[i:i+2] == '//': comment = True
        elif c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0: return i + 1
    raise AssertionError('unterminated member')


def members(source):
    result = {}
    for m in re.finditer(r'^    (function|execute|evaluate) (\w+)[^\n]*\{', source, re.M):
        end = closing(source, source.index('{', m.start()))
        key = (m[1], m[2])
        assert key not in result, key
        result[key] = source[m.start():end]
    return result


def execution_sections(source):
    begin = source.index('execution_model {\n')
    contract = source.index('execution_contract {\n', begin)
    end = closing(source, source.index('{', contract))
    return begin, end


def canonical(model):
    result = {}
    for key in ('context', 'state', 'declarations', 'properties'):
        result[key] = model[key]
    for key in ('functions', 'intrinsics', 'execute', 'evaluate'):
        items = copy.deepcopy(model[key])
        for v in items: v.pop('origin', None)
        result[key] = sorted(items, key=lambda v: v['signature']['name'] if 'signature' in v else v['pattern']['variant'])
    return result


def rust_members(text):
    functions = {}
    first = None
    last = None
    for m in re.finditer(r'^    fn (\w+)[^\n]*\{', text, re.M):
        end = closing(text, text.index('{', m.start()))
        name = m[1]
        assert name not in functions
        if first is None: first = m.start()
        fragment = text[m.start():end]
        if name in ('execute_generated', 'evaluate_generated'):
            arms = {}
            intervals = []
            for a in re.finditer(r'^            ([A-Za-z_][^\n]+) => \{', fragment, re.M):
                stop = closing(fragment, fragment.index('{', a.start() + len(a[1])))
                assert a[1] not in arms
                arms[a[1]] = fragment[a.start():stop]
                intervals.append((a.start(),stop))
            assert arms
            frame=fragment
            for begin,arm_end in reversed(intervals): frame=frame[:begin]+frame[arm_end:]
            fragment = (frame, arms)
        functions[name] = fragment
        last = end
    assert first is not None
    return text[:first], functions, text[last:]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--coge', type=Path, required=True)
    parser.add_argument('--sema', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    binary = str(args.coge.resolve())
    root = args.root.resolve()
    # Freeze the pre-profile Rust independently of regenerated repository outputs.
    baseline = json.loads((root / 'tests/execution_full_rust_v1.sha256.json').read_text())
    assert baseline['format'] == 'coge-explicit-execution-rust-sha256-v1'
    authored = {lang: (root / f'{lang}_typed.coge').read_text() for lang in ('toyc', 'toycp')}
    sources = {}
    # The unchanged legacy descriptions retain the independent explicit bodies.
    # Reuse only execution sections, keeping current semantic/lowering contracts.
    for lang, source in authored.items():
        legacy = (root / f'{lang}_typed.sema').read_text()
        begin, end = execution_sections(source)
        legacy_begin, legacy_end = execution_sections(legacy)
        sources[lang] = source[:begin] + legacy[legacy_begin:legacy_end] + source[end:]
    manifests = {lang: json.loads((root / f'contracts/{lang}-runtime-v1.json').read_text()) for lang in sources}
    blocks = {lang: members(source) for lang, source in sources.items()}
    common = [key for key, block in blocks['toyc'].items() if blocks['toycp'].get(key) == block]
    assert len(common) == 62
    short = {}
    for lang, source in sources.items():
        for key in common: source = source.replace(blocks[lang][key] + '\n', '', 1)
        short[lang] = source.replace('execution_model {\n', 'execution_model {\n    execution_profile shared_value_v1;\n', 1)
        assert short[lang] == authored[lang], (lang, 'migration changed local declarations or bodies')
    checks = 0
    with tempfile.TemporaryDirectory(prefix='execution-profiles-') as directory:
        work = Path(directory)
        def run(name, lang='toyc', source=None, manifest=None, op='--check', error=None, sema=False):
            nonlocal checks
            path = work / f'{name}.coge'
            path.write_text(short[lang] if source is None else source)
            contract = work / f'{name}.json'
            contract.write_text(json.dumps(manifests[lang] if manifest is None else manifest))
            out = work / f'{name}-out'
            cmd = [str(args.sema.resolve()) if sema else binary, op]
            if op not in ('--check', '--check-complete'): cmd.append(str(out))
            cmd += ['--contracts', str(contract), str(path)]
            p = subprocess.run(cmd, capture_output=True, text=True)
            if error:
                assert p.returncode != 0 and error in p.stderr, (name, p.stdout, p.stderr)
                assert not out.exists(), (name, 'invalid input wrote artifacts')
            else: assert p.returncode == 0, (name, p.stdout, p.stderr)
            checks += 1
            return out

        models = {}
        for lang in sources:
            full = json.loads(run(lang+'-full',lang,source=sources[lang],op='--inspect-model').read_text())['execution_model']
            model = json.loads(run(lang+'-short',lang,op='--inspect-model').read_text())['execution_model']
            models[lang] = model
            assert canonical(full) == canonical(model), lang
            assert len(model['functions']) == (54 if lang == 'toyc' else 51)
            assert len(model['execute']) == (12 if lang == 'toyc' else 13)
            assert len(model['evaluate']) == 8
            assert len(model['properties']['checks']) == 9
            assert sum(v['origin']['kind']=='profile_default' for v in model['functions']) == 46
            assert all(v['origin']['kind']=='profile_default' for v in model['evaluate'] if v['pattern']['variant']=='Load')
            assert len(model['dependencies']) > 100
            assert any(d.get('contract',{}).get('path','').endswith('/variants/Add') for d in model['dependencies'])
            assert any(d['kind']=='binding' and d.get('type',{}).get('name')=='ExprId' for d in model['dependencies'])
            assert any(d['member'].startswith('function/') and '/statements/' in d['path'] for d in model['dependencies'])
            load = next(f for f in model['functions'] if f['signature']['name']=='load')
            assert load['signature']['mutates'] == (lang == 'toycp')
            repeated = json.loads(run(lang+'-repeat',lang,op='--inspect-model').read_text())['execution_model']
            assert model == repeated
            full_dir = run(lang+'-full-rust',lang,source=sources[lang],op='--emit-rust-dir')
            short_dir = run(lang+'-short-rust',lang,op='--emit-rust-dir')
            names = sorted(p.name for p in full_dir.glob('*.rs'))
            assert names == sorted(p.name for p in short_dir.glob('*.rs'))
            assert names == sorted(baseline['files'][lang]), (lang, 'baseline file set changed')
            for name in names:
                actual = (full_dir/name).read_bytes()
                assert hashlib.sha256(actual).hexdigest() == baseline['files'][lang][name], (lang,name,'full path changed')
                other = (short_dir/name).read_bytes()
                saved = (root/f'examples/{lang}_typed/generated'/name).read_bytes()
                assert saved == other, (lang,name,'repository output is stale')
                if name == 'interpreter_gen.rs': assert rust_members(actual.decode()) == rust_members(other.decode()), lang
                else: assert actual == other, (lang,name)
            provenance=json.loads((short_dir/'sema_generation.provenance.json').read_text())
            assert provenance['execution_profiles'][0]['sha256']==model['profile']['sha256']
            assert json.loads((full_dir/'sema_generation.provenance.json').read_text())['execution_profiles']==[]
            a=run(lang+'-full-sema',lang,source=sources[lang],op='--emit-sema').read_bytes()
            b=run(lang+'-short-sema',lang,op='--emit-sema').read_bytes()
            assert a==b
            a=run(lang+'-full-ag',lang,source=sources[lang],op='--emit-ag').read_bytes()
            b=run(lang+'-short-ag',lang,op='--emit-ag').read_bytes()
            assert a==b
            full_calls=run(lang+'-full-calls',lang,source=sources[lang],op='--calls').read_text()
            short_calls=run(lang+'-short-calls',lang,op='--calls').read_text()
            assert full_calls==short_calls, (lang,set(full_calls.splitlines())^set(short_calls.splitlines()))
        assert models['toyc']['profile']['sha256']==models['toycp']['profile']['sha256']
        sema_dir=work/'sema-rust'
        subprocess.run([str(args.sema.resolve()),'--emit-rust-dir',str(sema_dir),'--contracts',str(root/'contracts/toyc-runtime-v1.json'),str(work/'toyc-short-sema-out')],check=True,capture_output=True,text=True)
        assert json.loads((sema_dir/'sema_generation.provenance.json').read_text())['execution_profiles']==[]
        checks += 1
        run('unknown',source=short['toyc'].replace('shared_value_v1','unknown_v1',1),error='coge.unknown_execution_profile')
        run('duplicate-profile',source=short['toyc'].replace('execution_profile shared_value_v1;','execution_profile shared_value_v1;\n    execution_profile shared_value_v1;',1),error='coge.duplicate_execution_profile')
        for name in ('initialize_global','value_matches_type','load'):
            run('missing-'+name,source=short['toyc'].replace(blocks['toyc'][('function',name)]+'\n','',1),op='--check-complete',error='completeness.pending')
        load = blocks['toyc'][('function','load')]
        intrinsic = '    intrinsic load(place: PlaceId, source: SourceRange) -> Result<Value, RuntimeError>;'
        run('intrinsic-required-body',source=short['toyc'].replace(load,intrinsic,1),op='--check-complete',error='completeness.pending')
        run('wrong-state',source=short['toyc'].replace('steps_remaining: Index','steps_remaining: I32',1),error='coge.execution_profile_contract_mismatch')
        run('wrong-store',source=short['toyc'].replace('store: Store<SymbolId, Value>','store: Store<SymbolId, Bool>',1),error='coge.execution_profile_contract_mismatch')
        run('wrong-value',source=short['toyc'].replace('Int(I32), Float','Int(Bool), Float',1),error='coge.execution_profile_contract_mismatch')
        run('missing-field',source=short['toyc'].replace(', steps_remaining: Index','',1),error='coge.execution_profile_contract_mismatch')
        run('missing-value-variant',source=short['toyc'].replace('Int(I32), ', '',1),error='coge.execution_profile_contract_mismatch')
        run('wrong-intrinsic',source=short['toyc'].replace('intrinsic literal_identity(expression: ExprId) -> Result<Index','intrinsic literal_identity(expression: ExprId) -> Result<Bool',1),error='coge.execution_profile_contract_mismatch')
        run('wrong-effect',source=short['toyc'].replace('intrinsic place_view(place: PlaceId) -> Result<PlaceView, RuntimeError>;','intrinsic place_view(place: PlaceId) -> Result<PlaceView, RuntimeError> mutates;',1),error='mutating call requires mutates')
        override=blocks['toyc'][('function','invalid_value')].replace('invalid semantic IR','custom invalid value')
        local=short['toyc'].replace('    properties {',override+'\n    properties {',1)
        overridden=json.loads(run('override',source=local,op='--inspect-model').read_text())['execution_model']
        assert overridden['profile']['sha256']==models['toyc']['profile']['sha256']
        assert overridden['effective_sha256']!=models['toyc']['effective_sha256']
        assert next(f for f in overridden['functions'] if f['signature']['name']=='invalid_value')['origin']['kind']=='profile_override'
        unchanged = canonical(overridden)
        expected = canonical(models['toyc'])
        unchanged['functions'] = [f for f in unchanged['functions'] if f['signature']['name']!='invalid_value']
        expected['functions'] = [f for f in expected['functions'] if f['signature']['name']!='invalid_value']
        assert unchanged == expected, 'local override changed unrelated model members'
        run('duplicate-member',source=local.replace(override,override+'\n'+override,1),error='coge.duplicate_execution_member')
        run('override-effect',source=local.replace('function invalid_value(source: SourceRange) -> Result<Value, RuntimeError> {','function invalid_value(source: SourceRange) -> Result<Value, RuntimeError> mutates {',1),error='coge.execution_profile_override_mismatch')
        run('override-type',source=local.replace('function invalid_value(source: SourceRange) -> Result<Value, RuntimeError> {','function invalid_value(source: SourceRange) -> Result<Unit, RuntimeError> {',1),error='coge.execution_profile_override_mismatch')
        run('override-parameter',source=local.replace('function invalid_value(source: SourceRange)','function invalid_value(source: I32)',1),error='coge.execution_profile_override_mismatch')
        load_override = blocks['toyc'][('evaluate','Load')]
        model=json.loads(run('override-load',source=short['toyc'].replace('execution_contract {','execution_contract {\n'+load_override,1),op='--inspect-model').read_text())['execution_model']
        assert canonical(model)==canonical(models['toyc'])
        assert next(v for v in model['evaluate'] if v['pattern']['variant']=='Load')['origin']['kind']=='profile_override'
        manifest=copy.deepcopy(manifests['toyc'])
        next(s for s in manifest['semantic'] if s['name']=='Operation')['variants']['Extra']=[]
        run('missing-handler',manifest=manifest,op='--check-complete',error='completeness.pending')
        run('missing-handler-emission', manifest=manifest, op='--emit-rust-dir',
            error='completeness.pending')
        # Explicit models require complete coverage even without a profile.
        for kind, variant in (('execute', 'Return'), ('evaluate', 'Constant')):
            missing = sources['toyc'].replace(blocks['toyc'][(kind, variant)] + '\n', '', 1)
            run('explicit-missing-' + variant, source=missing, op='--emit-rust-dir',
                error='completeness.pending')
        additional=short['toyc'].replace('execution_contract {','execution_contract {\n    execute Extra() { no_op(); }',1)
        run('extra-handler',source=additional,manifest=manifest)
        manifest=copy.deepcopy(manifests['toyc'])
        next(s for s in manifest['semantic'] if s['name']=='ExpressionKind')['variants']['Extra']=[]
        run('missing-evaluate',manifest=manifest,op='--check-complete',error='completeness.pending')
        additional=short['toyc'].replace('execution_contract {','execution_contract {\n    evaluate Extra() { return invalid_value(source); }',1)
        run('extra-evaluate',source=additional,manifest=manifest)
        manifest=copy.deepcopy(manifests['toyc'])
        next(s for s in manifest['semantic'] if s['name']=='Operation')['variants']['Initialize'][0]={'name':'Bool'}
        run('wrong-operation-payload',manifest=manifest,error='lowering variant payload mismatch: Operation.Initialize')
        manifest=copy.deepcopy(manifests['toyc'])
        manifest['execution']=[s for s in manifest['execution'] if s['name']!='AccessError']
        run('missing-access-error',manifest=manifest,error='variant has no execution contract: AccessError.MissingRoot')
        manifest=copy.deepcopy(manifests['toyc'])
        next(s for s in manifest['semantic'] if s['name']=='Operator')['variants']['Power']=[]
        run('nonexhaustive',manifest=manifest,error='coge.nonexhaustive_execution_match')
        manifest=copy.deepcopy(manifests['toyc'])
        next(s for s in manifest['semantic'] if s['name']=='RuntimeCheck')['variants']={'Other':[]}
        replacement='    function binary_checked(operator: Operator, right: ExprId, lhs: Value, rhs: Value, checks: List<RuntimeCheck>, source: SourceRange) -> Result<Value, RuntimeError> { return binary(operator, lhs, rhs, source); }\n'
        no_check=short['toyc'].replace('    properties {',replacement+'    properties {',1)
        run('unused-default-dependency',source=no_check,manifest=manifest,op='--check-complete')
        no_selection=short['toyc'].replace('    generate_interpreter = true;','')
        run('check-without-selection',source=no_selection)
        run('invalid-without-selection',source=no_selection.replace('shared_value_v1','unknown_v1',1),error='coge.unknown_execution_profile')
        # Projection does not prepare the deleted execution profile.
        run('projection-unknown',source=short['toyc'].replace('shared_value_v1','unknown_v1',1),op='--emit-sema')
        word="coge Names; grammar Names; node execution_profile : ID; ID : 'x';"
        run('keyword-outside-model',source=word,manifest={'format':1,'id':'empty','version':'1'})
        print(f'Execution profiles: {checks} cases passed; full models and Rust match, controlled member permutation only.')


if __name__=='__main__': main()

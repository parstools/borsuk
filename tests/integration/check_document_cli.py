"""Stage 3 CLI boundaries, isolated projections, provenance and writes."""
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    for name in ('sema', 'coge', 'ag', 'agas', 'fixtures', 'repo'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    def run(binary, *arguments, ok=True, code=None):
        result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, text=True)
        assert (result.returncode == 0) == ok, result.stdout+result.stderr
        if code:
            assert code in result.stderr, result.stderr
        return result
    with tempfile.TemporaryDirectory(prefix='agsem-stage3-') as temporary:
        root = Path(temporary)
        for language in ('toyc', 'toycp'):
            source = root/(language+'.coge')
            contract = root/(language+'.json')
            shutil.copyfile(args.fixtures/source.name, source)
            shutil.copyfile(args.repo/'contracts'/(language+'-runtime-v1.json'), contract)
            before = set(root.iterdir())
            run(args.coge, '--check', '--contracts', contract, source)
            assert set(root.iterdir()) == before, 'check wrote files'
            sema = root/(language+'.sema')
            ag = root/(language+'.ag')
            run(args.coge, '--emit-sema', sema, '--contracts', contract, source)
            run(args.coge, '--emit-ag', ag, source)
            for output in (sema, ag):
                manifest = Path(str(output)+'.provenance.json')
                initial = (output.read_bytes(), manifest.read_bytes())
                metadata = json.loads(initial[1])
                assert metadata['output_sha256'] == hashlib.sha256(initial[0]).hexdigest()
                assert metadata['source_sha256'] == hashlib.sha256(source.read_bytes()).hexdigest()
                assert metadata['tool_sha256'] == hashlib.sha256(args.coge.read_bytes()).hexdigest()
                operation = '--emit-sema' if output == sema else '--emit-ag'
                extra = ['--contracts', contract] if output == sema else []
                run(args.coge, operation, output, *extra, source, ok=False)
                run(args.coge, '--force', operation, output, *extra, source)
                assert initial == (output.read_bytes(), manifest.read_bytes()), 'nondeterministic projection'
            run(args.coge, '--force', '--emit-ag', source, source, ok=False, code='output.would_overwrite_input')
            alias = root/'alias'
            alias.symlink_to(source)
            run(args.coge, '--force', '--emit-ag', alias, source, ok=False, code='output.would_overwrite_input')
            alias.unlink()
            run(args.coge, '--force', '--emit-sema', contract, '--contracts', contract, source, ok=False, code='output.would_overwrite_input')
            run(args.sema, '--check', '--contracts', contract, source, ok=False, code='document.wrong_kind')
            run(args.coge, sema, ok=False, code='document.wrong_kind')
            generated = root/(language+'-coge-rust')
            run(args.coge, '--emit-rust-dir', generated, '--contracts', contract, source)
            source.unlink()
            # These exports have no author input or neighboring Ag to reopen.
            run(args.sema, '--contracts', contract, sema)
            alternate = root/(language+'-via-sema.ag')
            run(args.sema, '--emit-ag', alternate, sema)
            assert alternate.read_bytes() == ag.read_bytes()
            run(args.ag, '--check', ag)
            a = run(args.ag, ag).stdout
            b = run(args.agas, ag).stdout
            assert a == b
            semantic_dir = root/(language+'-sema-rust')
            run(args.sema, '--emit-rust-dir', semantic_dir, '--contracts', contract, sema)
            assert not (semantic_dir/'interpreter_gen.rs').exists()
            assert 'interpreter' not in (semantic_dir/'sema_modules.manifest').read_text()
            for file in semantic_dir.glob('*_gen.rs'):
                assert file.read_bytes() == (generated/file.name).read_bytes(), file
            if language == 'toyc':
                foreign_dir = root/'foreign-rust'
                foreign_dir.mkdir()
                module = next(file.name for file in semantic_dir.glob('sema_*_gen.rs') if file.name not in ('sema_lib_gen.rs',))
                (foreign_dir/module).write_text('// authored module\n')
                run(args.sema, '--emit-rust-dir', foreign_dir, '--contracts', contract, sema, ok=False, code='unowned module')
                assert (foreign_dir/module).read_text() == '// authored module\n'
                assert not (foreign_dir/'sema_gen.rs').exists(), 'wrote files before ownership validation'
                retained_backend = semantic_dir/'interpreter_gen.rs'
                retained_backend.write_text('// owned by coge\n')
                stale = semantic_dir/'sema_obsolete_gen.rs'
                stale.write_text('// Generated by sema\n')
                manifest = semantic_dir/'sema_modules.manifest'
                manifest.write_text(manifest.read_text()+stale.name+'\n')
                run(args.sema, '--emit-rust-dir', semantic_dir, '--contracts', contract, sema)
                assert not stale.exists()
                assert retained_backend.read_text() == '// owned by coge\n'
            # A provenance manifest is the last commit marker, never a directory.
            interrupted = root/'interrupted.ag'
            marker = Path(str(interrupted)+'.provenance.json')
            marker.mkdir(exist_ok=True)
            run(args.sema, '--force', '--emit-ag', interrupted, sema, ok=False)
            assert not marker.is_file()
        run(args.sema, '--legacy', args.repo/'tests/fixtures/legacy/minimal.sema', ok=False, code='coge --legacy')
        run(args.coge, '--legacy', args.repo/'tests/fixtures/legacy/minimal.sema')
        # Invalid execution bodies are checked even without an interpreter request;
        # a parser projection remains available and semantic closure is enough for sema.
        contract = root/'context.json'
        contract.write_text(json.dumps({'format':1,'id':'context','version':'1','semantic':[{'name':'Context','kind':'opaque'}]}))
        prefix = 'coge Check; grammar Check; semantic_model { rust_context Context; analyzer start() -> Int; } node start : ID analysis { result = 1; }; ID:\'a\'; '
        for declaration, diagnostic in (
            ('function unused() -> Result<Unit, Missing> { return unit; }', 'missing execution type'),
            ('record RuntimeError(message: Text, source: SourceRange); function unused() -> Result<Unit, RuntimeError> { unknown(); return unit; }', 'unresolved execution function'),
            ('record RuntimeError(message: Text, source: SourceRange); function unused() -> Result<I32, RuntimeError> { return true; }', 'type mismatch'),
            ('record RuntimeError(message: Text, source: SourceRange); function f(value: Bool) -> Result<Unit, RuntimeError> { return unit; } function unused() -> Result<Unit, RuntimeError> { return f(1); }', 'type mismatch'),
            ('record RuntimeError(message: Text, source: SourceRange); function f() -> Result<Unit, RuntimeError> mutates { return unit; } function unused() -> Result<Unit, RuntimeError> { return f(); }', 'requires mutates'),
        ):
            source = root/'bad.coge'
            source.write_text(prefix+'execution_model { '+declaration+' }')
            run(args.coge, '--contracts', contract, source, ok=False, code=diagnostic)
            run(args.coge, '--force', '--emit-sema', root/'bad.sema', '--contracts', contract, source)
            run(args.coge, '--force', '--emit-ag', root/'bad.ag', source)
        result_source = root/'result.coge'
        result_source.write_text(prefix.replace("ID analysis { result = 1; }", "ID analysis { result = 1; } execution result {}"))
        run(args.coge, '--contracts', contract, result_source, ok=False, code='coge.unsupported_execution_result')
        run(args.coge, '--force', '--emit-sema', root/'result.sema', '--contracts', contract, result_source)
    # Check the actual linked binary as well as CMake target declarations.
    symbols = subprocess.check_output(['nm', '-C', str(args.sema)], text=True)
    assert 'coge::' not in symbols, 'sema linked coge symbols'
    print('CLI isolation, projections, checks, ownership and provenance OK')

if __name__ == '__main__':
    main()

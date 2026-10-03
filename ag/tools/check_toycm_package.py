#!/usr/bin/env python3
"""Check reproducible exports in isolated directories and preserve existing data."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def files(directory):
    return {path.name: path.read_bytes() for path in directory.iterdir()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agas', type=Path, required=True)
    parser.add_argument('--grammar', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='agas-toycm-export-') as temporary:
        root = Path(temporary)

        def export(output, ok=True):
            result = subprocess.run(
                [str(args.agas.resolve()), '--emit-package', str(output),
                 str(args.grammar.resolve())], capture_output=True, text=True)
            assert (result.returncode == 0) == ok, result.stdout + result.stderr
            if not ok:
                assert 'already exists' in result.stderr, result.stderr

        first, second = root / 'first', root / 'second'
        export(first)
        export(second)
        baseline = files(first)
        assert baseline == files(second), 'exports differ in names or bytes'
        manifest = json.loads(baseline['manifest.json'])
        assert len(manifest['sections']) == 7
        assert set(baseline) == {'manifest.json', *(s['file'] for s in manifest['sections'])}
        for section in manifest['sections']:
            data = baseline[section['file']]
            assert len(data) == section['byteLength']
            assert hashlib.sha256(data).hexdigest() == section['sha256']
        export(first, ok=False)
        assert files(first) == baseline, 'refused export changed an existing package'
        occupied = root / 'occupied'
        occupied.mkdir()
        (occupied / 'user.txt').write_bytes(b'user-owned data\n')
        before = files(occupied)
        export(occupied, ok=False)
        assert files(occupied) == before, 'refused export changed user-owned data'
    print('ToyCM sections=7: identical exports; existing packages and user data preserved')


if __name__ == '__main__':
    main()

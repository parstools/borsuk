"""Argument diagnostics for migrated example checks."""

import argparse


class MigratedCogeParser(argparse.ArgumentParser):
    def __init__(self, language="toyc", **kwargs):
        self.migration = (
            f"Migration: use --coge PATH --source coge/examples/{language}/{language}.coge "
            f"--contracts contracts/{language}-runtime-v1.json. "
            "--sema PATH remains an alias for --coge PATH and expects the coge binary. "
            "Old mixed inputs can be run directly with coge --legacy FILE. "
            "See docs/DOCUMENT_CLI.md."
        )
        super().__init__(epilog=self.migration, **kwargs)

    def error(self, message):
        super().error(message + "\n" + self.migration)


def explicit_model_bindings(path):
    """Restore explicit binding fixtures while retaining the migrated source."""
    import re
    source = path.read_text().replace('    model_schema standard_semantic_v1;\n', '')
    legacy = path.with_suffix('.sema').read_text()
    pattern = r'    (?:model|assignment|condition|statement|flow|selection)_bindings [^\n]+ \{\n.*?    \}\n'
    for match in re.finditer(pattern, legacy, re.DOTALL):
        block = match.group()
        header = block.splitlines()[0]
        start = source.find(header)
        if start >= 0:
            end = source.index('\n    }\n', start) + 7
            source = source[:start] + block + source[end:]
        else:
            family = header.strip().split('_bindings')[0]
            policy = {'model': 'return_policy', 'assignment': 'assignment_policy', 'condition': 'condition_policy', 'statement': 'statement_ir', 'flow': 'flow_actions', 'selection': 'selection_policy'}[family]
            start = source.index('    ' + policy + ' ')
            # Keep the binding adjacent to its policy, including its errors block.
            end = source.index('\n    }\n', start) + 7
            source = source[:end] + block + source[end:]
    return source

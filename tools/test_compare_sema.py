"""Focused tests for structural comparison and lossless reordering."""

import unittest
from collections import Counter

from compare_sema import compare, extract_rules, reordered_text, tokenize


class CompareSemaTests(unittest.TestCase):
    def test_reorders_rules_and_alternatives_with_actions(self):
        grammar = """grammar Demo;
node start
    : first=choice EOF
    ;

inline choice
    : value=A #First
    | value=B #Second
    ;
"""
        sema = """sema Demo for "demo.ag";
inline choice
    : value=B #Second
        /* B action */ analysis { let b = call({nested: 1}); }
    | value=A #First
        /* A action */ analysis { result = a; }
    ;

node start
    : first=choice EOF analysis { result = first; }
    ;
"""
        source_rules = extract_rules(grammar, semantic=False)
        sema_rules = extract_rules(sema, semantic=True)
        messages, compatible = compare(source_rules, sema_rules)
        self.assertTrue(compatible)
        self.assertTrue(any("WARNING rule order" in message for message in messages))
        self.assertTrue(any("move alternatives to positions [2, 1]" in message
                            for message in messages))
        output = reordered_text(source_rules, sema_rules, sema)
        reordered_rules = extract_rules(output, semantic=True)
        self.assertEqual([rule.name for rule in reordered_rules], ["start", "choice"])
        self.assertEqual([alt.signature for alt in reordered_rules[1].alternatives],
                         [alt.signature for alt in source_rules[1].alternatives])
        self.assertEqual(Counter(token.value for token in tokenize(output)),
                         Counter(token.value for token in tokenize(sema)))
        self.assertIn("/* B action */", output)
        self.assertIn("/* A action */", output)

    def test_rejects_changed_alternative(self):
        grammar = "node start : A | B ;"
        sema = "node start : A analysis {} | C analysis {} ;"
        messages, compatible = compare(extract_rules(grammar, False),
                                       extract_rules(sema, True))
        self.assertFalse(compatible)
        self.assertTrue(any("missing alternative B" in message for message in messages))
        self.assertTrue(any("extra alternative C" in message for message in messages))

    def test_rejects_inter_rule_text_when_reordering(self):
        grammar = "node first : A ;\nnode second : B ;"
        sema = "node second : B ;\n/* belongs to first? */\nnode first : A ;"
        source_rules = extract_rules(grammar, False)
        sema_rules = extract_rules(sema, True)
        with self.assertRaisesRegex(ValueError, "text between parser rules"):
            reordered_text(source_rules, sema_rules, sema)


if __name__ == "__main__":
    unittest.main()

"""Skill presentation extraction must respect flags, graph references and assets."""
import struct
import unittest
from import_skill_effects import NODE, ROOT, LIST
from import_skill_presentation import presentation, missile_visual


def graph(hooks):
    fields = [0] * 66
    fields[65] = 1
    count = 2 + len(hooks)
    words = [0, 0, count] + [0] * count
    words += [ROOT, 0, 0, len(fields), *fields]
    words += [NODE, 0, LIST, len(hooks), *range(2, count)]
    for hook in hooks:
        words += [NODE, 0, LIST, len(hook), *hook]
    return struct.pack(f'<{len(words)}I', *words)


def hook(flags=0x2040, cast=0x81000281, release=0x410004a9, missile=0x47000d04):
    values = [0] * 32
    values[6], values[18], values[19], values[20] = missile, cast, flags, release
    values[15] = 0x41000241  # Target FX is not an attacker cast or release.
    return values


class SkillPresentation(unittest.TestCase):
    def test_hook_flags_and_sides(self):
        self.assertEqual(presentation(graph([hook()])), dict(
            cast_fx=0x81000281, release_fx=0x410004a9, missile_entity=0x47000d04))
        result = presentation(graph([hook(flags=0)]))
        self.assertEqual(result['cast_fx'], 0)
        self.assertEqual(result['release_fx'], 0)
        self.assertEqual(presentation(graph([hook(flags=0x40)]))['release_fx'], 0)
        self.assertEqual(presentation(graph([hook(flags=0x2000)]))['cast_fx'], 0)

    def test_repeated_cues_deduplicate_but_distinct_hooks_are_not_guessed(self):
        self.assertEqual(presentation(graph([hook(), hook()])), presentation(graph([hook()])))
        with self.assertRaisesRegex(ValueError, 'multiple distinct'):
            presentation(graph([hook(), hook(missile=0x47000306)]))
        with self.assertRaisesRegex(ValueError, 'invalid FX'):
            presentation(graph([hook(cast=0x1f000001)]))
        with self.assertRaisesRegex(ValueError, 'truncated'):
            presentation(graph([[1, 2]]))

    def test_missile_prefix_layout(self):
        def pack(*values):
            return struct.pack(f'<{len(values)}I', *values)
        self.assertEqual(missile_visual(pack(0x8b, 0x47000d04, 0x40000001, 0x1f000b85)), 0x1f000b85)
        self.assertEqual(missile_visual(pack(0x8f, 0x47000d04, 0x40000001, 7, 0, 0x1f000b85)), 0x1f000b85)
        with self.assertRaises(ValueError):
            missile_visual(pack(0x8b, 0x47000d04, 0x40000001, 0))
        with self.assertRaises(ValueError):
            missile_visual(pack(0x83, 0x47000d04, 0x40000001))


if __name__ == '__main__':
    unittest.main()

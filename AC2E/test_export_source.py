"""Ensure published source offers contain build inputs and exclude local state."""
from pathlib import Path
import tempfile
import unittest
import zipfile

from export_source import export, SUPPORT_FILES


class SourceOfferTests(unittest.TestCase):
    def test_clean_checkout_and_unpacked_offer(self):
        with tempfile.TemporaryDirectory() as temp:
            workspace = Path(temp)
            server = workspace / 'AC2E'
            required = [
                'LICENSE', 'NOTICE', 'Build-AC2E.ps1', 'export_source.py',
                'emulator/Cargo.toml', 'emulator/Cargo.lock', 'emulator/build.rs',
                'emulator/src/server.rs', 'emulator/content/world.tsv',
                'emulator/docs/architecture.md', 'emulator/tools/prepare.py',
                'emulator/web/accounts/index.html', 'deploy/Caddyfile',
                'deploy/online.env.example', 'LICENSES/third-party/example.txt',
            ]
            excluded = [
                'data/accounts.journal', 'data/characters.json', '.env',
                'emulator/target/test.exe', 'emulator/tools/__pycache__/test.pyc',
                'emulator/content/portal.dat', 'emulator/content/.env.local',
                'emulator/source/old.zip', 'emulator/web/accounts/private.key',
            ]
            for relative in required + excluded:
                path = server / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture', encoding='utf-8')
            for relative in SUPPORT_FILES:
                path = workspace / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture', encoding='utf-8')
            output = export(server)
            with zipfile.ZipFile(output) as archive:
                names = set(archive.namelist())
                self.assertEqual(names, {'AC2E/' + p for p in required} | set(SUPPORT_FILES))
                unpacked = workspace / 'unpacked'
                archive.extractall(unpacked)
            with zipfile.ZipFile(export(unpacked / 'AC2E')) as archive:
                self.assertEqual(set(archive.namelist()), names)


if __name__ == '__main__':
    unittest.main()

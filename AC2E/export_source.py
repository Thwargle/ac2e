"""Create the corresponding AC2E source archive served by the account backend."""
from pathlib import Path
import zipfile


SOURCE_TREES = (
    'emulator/src', 'emulator/tests', 'emulator/benches', 'emulator/content',
    'emulator/tools', 'emulator/web', 'emulator/docs', 'deploy', 'LICENSES',
)
SOURCE_SUFFIXES = {
    '.rs', '.toml', '.py', '.ps1', '.sh', '.bat', '.cmd', '.md', '.txt', '.tsv',
    '.json', '.html', '.css', '.js', '.service', '.slice', '.conf', '.example',
}
SOURCE_NAMES = {'LICENSE', 'NOTICE', 'Cargo.lock', 'Caddyfile', '.gitignore', '.gitattributes'}
EXCLUDED_PARTS = {'target', 'dist', 'data', '__pycache__', '.git', 'source'}
SUPPORT_FILES = ('Network/prepare_content.py', 'Network/content_profile.py',
                 'Tools/AC2/dat_probe.py', 'Tools/AC2/population_probe.py')


def source_files(root):
    """Work identically in a monorepo and an unpacked source offer.

    Use only source directories, including current uncommitted edits. Never
    traverse an operator's stores or package game archives/build output.
    """
    root = root.resolve()
    candidates = list(root.iterdir()) + list((root / 'emulator').iterdir())
    for tree in SOURCE_TREES:
        candidates.extend((root / tree).rglob('*'))
    for path in sorted(set(candidates)):
        relative = path.relative_to(root)
        if not path.is_file() or path.is_symlink() or path.resolve() != path:
            continue
        if any(part in EXCLUDED_PARTS or part.startswith('.env') for part in relative.parts):
            continue
        if path.name in SOURCE_NAMES or path.suffix.lower() in SOURCE_SUFFIXES:
            yield path, 'AC2E/' + relative.as_posix()


def export(root):
    root = root.resolve()
    output = root / 'emulator/source/AC2E-source.zip'
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path, name in source_files(root):
            archive.write(path, name)
        for relative in SUPPORT_FILES:
            archive.write(root.parent / relative, relative)
    return output


if __name__ == '__main__':
    output = export(Path(__file__).resolve().parent)
    print(f'AC2E source offer: {output.name} ({output.stat().st_size} bytes)')

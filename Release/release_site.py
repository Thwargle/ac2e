"""Generate the public page and updater feed from the finished installer."""
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

def generate(output, version):
    if not re.fullmatch(r'\d{1,4}\.\d{1,4}\.\d{1,4}', version):
        raise ValueError('Invalid product version')
    filename = f'AC2-Unreal-Setup-{version}.exe'
    installer = output / filename
    size = installer.stat().st_size
    with installer.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    release = f'https://thwargle.com/downloads/ac2/v{version}/'
    notes = release + 'RELEASE-NOTES.txt'
    feed = dict(product='ac2-unreal-windows-x64', version=version, installer=release+filename, bytes=size, sha256=digest)
    site = output / 'site'
    site.mkdir(exist_ok=True)
    template = Path(__file__).parent / 'site'
    text = (template/'index.html').read_text(encoding='utf-8')
    for key, value in dict(INSTALLER=feed['installer'], VERSION=version, SIZE=f'{size/1048576:.1f} MiB', HASH=digest, NOTES=notes, CHECKSUM=release+'SHA256SUMS.txt').items():
        text = text.replace('@'+key+'@',value)
    (site/'index.html').write_text(text,encoding='utf-8')
    (site/'release.json').write_text(json.dumps(feed, indent=2)+'\n',encoding='utf-8')
    shutil.copyfile(Path(__file__).parent / 'notes' / (version + '.txt'), output / 'RELEASE-NOTES.txt')
    for filename in ['site.css','keyart.png']:
        shutil.copyfile(template/filename, site/filename)
    (output/'SHA256SUMS.txt').write_text(digest+'  '+installer.name+'\n',encoding='ascii')
    print(f'{installer.name}  {size} bytes  SHA-256 {digest}')

if __name__ == '__main__':
    generate(Path(sys.argv[1]), sys.argv[2])


"""Collect notices from the actual locked Windows Rust dependency graphs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

def collect(root, notices):
    cargo = 'cargo'
    env = dict(os.environ)
    local = root / 'Tools/Rust/cargo/bin/cargo.exe'
    if local.is_file():
        cargo = str(local)
        env.update(CARGO_HOME=str(root/'Tools/Rust/cargo'), RUSTUP_HOME=str(root/'Tools/Rust/rustup'))
        env['PATH'] = str(local.parent) + os.pathsep + env['PATH']
    packages = {}
    for manifest in ['Network/Bridge/Cargo.toml','Audio/Bridge/Cargo.toml']:
        raw = subprocess.check_output([cargo,'metadata','--locked','--format-version','1','--filter-platform','x86_64-pc-windows-msvc','--manifest-path',str(root/manifest)],env=env)
        metadata = json.loads(raw)
        nodes = {n['id']:n for n in metadata['resolve']['nodes']}
        pending = [metadata['resolve']['root']]
        included = set()
        while pending:
            current = pending.pop()
            if current in included: continue
            included.add(current)
            for dep in nodes[current]['deps']:
                if any(k['kind'] != 'dev' for k in dep['dep_kinds']): pending.append(dep['pkg'])
        packages.update((p['id'],p) for p in metadata['packages'] if p['id'] in included)
    out = notices/'Rust'; out.mkdir(parents=True,exist_ok=True)
    lines = []
    for p in sorted(packages.values(),key=lambda p:(p['name'],p['version'])):
        key = p['name']+'-'+p['version']
        folder = Path(p['manifest_path']).parent
        texts = [f for f in folder.iterdir() if f.is_file() and f.name.lower().startswith(('license','licence','copying','notice','copyright'))]
        if p.get('license_file'): texts.append(folder/p['license_file'])
        for directory in [folder/'licenses',folder/'LICENSES']:
            if directory.is_dir(): texts.extend(f for f in directory.rglob('*') if f.is_file())
        for f in set(texts):
            dest = out/key/f.relative_to(folder) if f.is_relative_to(folder) else out/key/f.name
            dest.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(f,dest)
        lines.append(f"{key}   {p.get('license') or 'See component license'}   {p.get('repository') or ''}")
    (out/'COMPONENTS.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')

if __name__ == '__main__':
    collect(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())


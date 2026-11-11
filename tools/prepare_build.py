"""Synchronize an ASCII-only build cache; the project directory stays authoritative."""
import argparse
import json
import shutil
from pathlib import Path

def prepare(project: Path, cache: Path):
    project=project.resolve(); cache=cache.resolve()
    if not str(cache).isascii() or cache==project or cache in project.parents or project in cache.parents:
        raise ValueError('Build cache must be ASCII and outside the project directory')
    marker=cache/'origin.json'
    if cache.exists() and not marker.exists() and any(cache.iterdir()):
        raise ValueError('Refusing to use a nonempty cache without an ownership marker')
    if marker.exists() and json.loads(marker.read_text(encoding='utf-8'))['project']!=str(project):
        raise ValueError('Build cache belongs to a different project')
    cache.mkdir(parents=True,exist_ok=True)
    marker.write_text(json.dumps({'project':str(project)},ensure_ascii=False),encoding='utf-8')
    snapshot=cache/'src'; snapshot.mkdir(exist_ok=True)
    managed=cache/'synced.json'
    previous=json.loads(managed.read_text()) if managed.exists() else []
    files=[]
    for folder in ['firmware/main','firmware/components','sdk/include','external/wamr/core','external/wamr/build-scripts']:
        files.extend(p for p in (project/folder).rglob('*') if p.is_file() and '.git' not in p.parts)
    files.extend(project/p for p in ['firmware/CMakeLists.txt','firmware/partitions.csv','firmware/sdkconfig.defaults'])
    current=[]
    for src in files:
        relative=src.relative_to(project); dst=snapshot/relative
        dst.parent.mkdir(parents=True,exist_ok=True)
        # Preserve content, but give changed cache files a fresh timestamp.
        # copy2() can leave them older than objects built during a long build.
        if not dst.exists() or src.read_bytes()!=dst.read_bytes(): shutil.copyfile(src,dst)
        current.append(relative.as_posix())
    for stale in set(previous)-set(current):
        dst=(snapshot/stale).resolve()
        if snapshot.resolve() not in dst.parents: raise ValueError('Invalid previous snapshot path')
        if dst.is_file(): dst.unlink()
    managed.write_text(json.dumps(current),encoding='utf-8')
    # Reuse downloaded package files once. Subsequent IDF runs own this cache.
    cached_components=snapshot/'firmware/managed_components'
    if not cached_components.exists() and (project/'firmware/managed_components').exists():
        shutil.copytree(project/'firmware/managed_components',cached_components)
    # SDK options are the only generated configuration intentionally imported.
    if (project/'firmware/sdkconfig').exists():
        source_config=project/'firmware/sdkconfig'
        cached_config=snapshot/'firmware/sdkconfig'
        if not cached_config.exists() or source_config.read_bytes()!=cached_config.read_bytes():
            shutil.copy2(source_config,cached_config)
    # Lock files contain local source paths. Let IDF resolve a fresh cache lock.
    return snapshot

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('project',type=Path); p.add_argument('cache',type=Path)
    a=p.parse_args(); print(prepare(a.project,a.cache))

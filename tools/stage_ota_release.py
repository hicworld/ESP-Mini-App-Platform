"""Stage a versioned OTA binary and reviewable publication plan locally.

Does not upload, flash, sign or modify a connected device. The plan explicitly
records that the URL is a proposed destination, not a verified public object.
"""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def stage(version: str, base_url: str):
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Version must be X.Y.Z')
    header = (ROOT / 'firmware/main/platform.h').read_text(encoding='utf-8')
    runner = (ROOT / 'Run.ps1').read_text(encoding='utf-8')
    if f'#define PLATFORM_VERSION "{version}"' not in header or f"$ProjectVersion='{version}'" not in runner:
        raise ValueError('Source and build version disagree')
    source = ROOT / 'dist/firmware/esp_mini_platform.bin'
    if not source.is_file():
        raise FileNotFoundError(source)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    checksums = (ROOT / 'dist/firmware/SHA256SUMS.txt').read_text(encoding='ascii')
    if f'{digest}  esp_mini_platform.bin' not in checksums:
        raise ValueError('Built binary does not match SHA256SUMS')
    name = f'esp_mini_platform-{version}.bin'
    target = ROOT / 'dist/ota' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != digest:
        raise ValueError('Existing versioned release has different contents')
    shutil.copyfile(source, target)
    plan = {
        'version': version,
        'file': name,
        'size': target.stat().st_size,
        'sha256': digest,
        'proposed_url': base_url.rstrip('/') + '/' + name,
        'published': False,
        'device_ota_verified': False,
    }
    (target.parent / f'{name}.sha256').write_text(f'{digest}  {name}\n', encoding='ascii')
    (target.parent / f'{name}.plan.json').write_text(
        json.dumps(plan, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    return plan


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', default='0.3.3')
    parser.add_argument('--base-url', default=(
        'https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com/firmware'))
    args = parser.parse_args()
    print(json.dumps(stage(args.version, args.base_url), indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()

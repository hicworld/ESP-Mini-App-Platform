"""Build the reviewable publication bundle for a staged OTA release.

Produces the versioned checksum list, the release notes and a deploy zip, then
reopens the zip and checks that the firmware inside matches the standalone file
byte for byte. The deployment notes do that re-open check for every historical
release, so it is part of the script rather than a manual step.

Run tools/stage_ota_release.py first: this reads the versioned binary it writes.
"""
import argparse
import hashlib
import json
import shutil
import zipfile
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha256_of(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def bundle(version, notes, base_url):
    ota = ROOT/'dist/ota'
    binary = ota/f'esp_mini_platform-{version}.bin'
    if not binary.is_file():
        raise FileNotFoundError(f'{binary} missing; run tools/stage_ota_release.py --version {version} first')
    plan_path = binary.with_suffix('.bin.plan.json')
    plan = json.loads(plan_path.read_text(encoding='utf-8')) if plan_path.is_file() else {}

    digest = sha256_of(binary)
    if plan and plan.get('sha256') not in (None, digest):
        raise ValueError('staged binary changed since it was staged')

    sums = ota/f'SHA256SUMS-{version}.txt'
    sums.write_text(f'{digest}  esp_mini_platform-{version}.bin\n', encoding='ascii')

    url = base_url.rstrip('/') + f'/esp_mini_platform-{version}.bin'
    header = [
        f'ESP Mini App Platform {version}',
        f'发布日期: {date.today().isoformat()}',
        '',
        f'固件文件: esp_mini_platform-{version}.bin',
        f'大小: {binary.stat().st_size} 字节',
        f'SHA-256: {digest}',
        f'拟发布地址: {url}',
        '',
        '验证状态: 未上传、未执行在线 OTA 验收。上传后须用公网地址重新下载逐字节核对，',
        '并在测试设备上确认 status 中的版本、分区与 ota_state=2，才可称为已发布。',
        '',
    ]
    body = Path(notes).read_text(encoding='utf-8') if notes else \
        '(发布说明待补: 用 --notes 提供变更内容)'
    release = ota/f'RELEASE-{version}.txt'
    release.write_text('\n'.join(header) + body.strip() + '\n', encoding='utf-8')

    zip_path = ROOT/'dist'/f'ota-deploy-{version}.zip'
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.write(binary, binary.name)
        archive.write(sums, sums.name)
        archive.write(release, release.name)

    # Re-open and confirm the archived firmware is identical to the standalone file.
    with zipfile.ZipFile(zip_path) as archive:
        archived = archive.read(binary.name)
    if hashlib.sha256(archived).hexdigest() != digest or len(archived) != binary.stat().st_size:
        raise ValueError('zip contents do not match the standalone firmware')

    result = {
        'version': version,
        'binary': binary.name,
        'size': binary.stat().st_size,
        'sha256': digest,
        'checksums': sums.name,
        'checksums_sha256': sha256_of(sums),
        'release_notes': release.name,
        'bundle': zip_path.name,
        'bundle_size': zip_path.stat().st_size,
        'bundle_sha256': sha256_of(zip_path),
        'proposed_url': url,
        'published': False,
        'device_ota_verified': False,
    }
    plan.update(result)
    plan_path.write_text(json.dumps(plan, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--version', default='0.4.4')
    parser.add_argument('--notes', help='file with the change description for RELEASE-<v>.txt')
    parser.add_argument('--base-url', default='https://esp-mini-app-1315743965.cos.ap-guangzhou.myqcloud.com')
    args = parser.parse_args()
    print(json.dumps(bundle(args.version, args.notes, args.base_url), indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()

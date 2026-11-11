"""Static first-pass review of an unsigned Developer Studio submission ZIP.

Never extracts files, executes submitted code, signs an app, or publishes a store.
The result is REJECTED or MANUAL_REVIEW; passing these checks is not approval.
"""
import argparse
import hashlib
import io
import json
import re
import subprocess
import sys
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

from app_package import prepare_candidate
from node_runtime import node_executable

ROOT = Path(__file__).resolve().parents[1]
MAX_ZIP = 12 * 1024 * 1024
MAX_UNPACKED = 11 * 1024 * 1024
MAX_FILES = 500
SLUG = re.compile(r'[a-z][a-z0-9_-]{1,39}\Z')
SHA_LINE = re.compile(r'([0-9a-f]{64})  ([^\r\n]+)\s*\Z')
SENSITIVE_SUFFIXES = {'.pem', '.key', '.p12', '.pfx', '.exe', '.dll', '.app'}
PERMISSION_FOR_IMPORT = {
    'clear': 'display', 'rect': 'display', 'text': 'display',
    'scene': 'display', 'image': 'display', 'audio': 'audio',
    'environment': 'environment', 'kv_get': 'storage', 'kv_set': 'storage',
    'imu': 'sensors', 'orientation': 'sensors', 'magnet': 'sensors',
    'battery': 'sensors', 'feedback': 'feedback',
}
ALLOWED_IMPORTS = set(PERMISSION_FOR_IMPORT) | {'hardware', 'millis', 'settings'}
NEW_050_IMPORTS = {'image', 'imu', 'orientation', 'magnet', 'battery', 'feedback'}
HARDWARE_FOR_IMPORT = {
    'clear': {'display'}, 'rect': {'display'}, 'text': {'display'},
    'scene': {'display'}, 'image': {'display'}, 'audio': {'speaker'},
    'imu': {'imu'}, 'orientation': {'imu'}, 'magnet': {'magnetometer'},
    'battery': {'battery'}, 'feedback': {'haptic', 'led'},
}


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate JSON key: {key}')
        result[key] = value
    return result


def finding(report, level, code, detail):
    report['findings'].append({'level': level, 'code': code, 'detail': detail})


def finish(report):
    report['status'] = ('REJECTED' if any(f['level'] == 'error' for f in report['findings'])
                        else 'MANUAL_REVIEW')
    report['counts'] = {level: sum(f['level'] == level for f in report['findings'])
                        for level in ('error', 'warning', 'pass')}
    return report


def inspect_imports(wasm, report):
    """Use trusted local checkers on a size-limited WASM; do not instantiate it."""
    with tempfile.TemporaryDirectory(prefix='emap-review-') as directory:
        path = Path(directory) / 'submitted.wasm'
        path.write_bytes(wasm)
        try:
            node = node_executable()
            abi = subprocess.run([node, str(ROOT / 'tools/abi_check.mjs'), str(path)],
                                 capture_output=True, text=True, errors='replace', timeout=15,
                                 cwd=ROOT)
        except (OSError, subprocess.TimeoutExpired) as exc:
            finding(report, 'error', 'abi_checker', f'ABI 检查无法完成：{exc}')
            return None
        if abi.returncode:
            finding(report, 'error', 'abi', (abi.stdout + abi.stderr)[-2000:].strip())
            return None
        finding(report, 'pass', 'abi', abi.stdout.strip())
        script = ('const fs=require("fs");'
                  'const m=new WebAssembly.Module(fs.readFileSync(process.argv[1]));'
                  'process.stdout.write(JSON.stringify(WebAssembly.Module.imports(m)));')
        try:
            result = subprocess.run([node, '-e', script, str(path)], capture_output=True,
                                    text=True, errors='replace', timeout=15, cwd=ROOT)
            if result.returncode:
                raise ValueError((result.stdout + result.stderr)[-1000:])
            return json.loads(result.stdout)
        except (OSError, subprocess.TimeoutExpired, ValueError, json.JSONDecodeError) as exc:
            finding(report, 'error', 'wasm_imports', f'无法读取 WASM 导入表：{exc}')
            return None


def check_capabilities(manifest, imports, report):
    permissions = set(manifest['permissions'])
    hardware = set(manifest['hardware']) | set(manifest.get('optional_hardware', []))
    used = set()
    for item in imports:
        module, name, kind = item['module'], item['name'], item['kind']
        if module != 'platform' or name not in ALLOWED_IMPORTS or kind != 'function':
            finding(report, 'error', 'unsupported_import', f'不支持的导入：{module}.{name} ({kind})')
            continue
        used.add(name)
        needed = PERMISSION_FOR_IMPORT.get(name)
        if needed and needed not in permissions:
            finding(report, 'error', 'missing_permission', f'{name} 需要 Manifest 权限 {needed}')
        choices = HARDWARE_FOR_IMPORT.get(name)
        if choices and not (choices & hardware):
            finding(report, 'error', 'missing_hardware',
                    f'{name} 需要在必需或可选外设声明：{", ".join(sorted(choices))}')
    if used & NEW_050_IMPORTS and tuple(map(int, manifest.get('platform_min', '0.1.0').split('.'))) < (0, 5, 0):
        finding(report, 'error', 'platform_min', '所用接口要求最低平台版本 0.5.0')
    if not any(f['level'] == 'error' and f['code'] in {'unsupported_import', 'missing_permission',
                                                       'missing_hardware', 'platform_min'}
               for f in report['findings']):
        finding(report, 'pass', 'capabilities',
                f'{len(imports)} 个导入的权限、外设和最低平台版本声明相符')


def audit_bytes(data, filename='submission.zip'):
    report = {'file': Path(filename).name, 'checked_at': datetime.now(timezone.utc).isoformat(),
              'zip_sha256': hashlib.sha256(data).hexdigest(), 'manifest': None, 'findings': []}
    if len(data) > MAX_ZIP:
        finding(report, 'error', 'archive_size', f'ZIP 超过 {MAX_ZIP // 1024 // 1024} MiB')
        return finish(report)
    try:
        archive = zipfile.ZipFile(io.BytesIO(data))
    except (zipfile.BadZipFile, OSError) as exc:
        finding(report, 'error', 'archive', f'无法读取 ZIP：{exc}')
        return finish(report)
    with archive:
        entries = archive.infolist()
        infos = [info for info in entries if not info.is_dir()]
        if not infos or len(entries) > MAX_FILES + 20 or len(infos) > MAX_FILES:
            finding(report, 'error', 'file_count', f'文件数必须为 1–{MAX_FILES}')
            return finish(report)
        if sum(info.file_size for info in infos) > MAX_UNPACKED:
            finding(report, 'error', 'unpacked_size', '解压后的文件总量超过 11 MiB')
            return finish(report)
        names, roots = set(), set()
        for info in entries:
            name = info.filename
            path = PurePosixPath(name)
            mode = (info.external_attr >> 16) & 0o170000
            parts = name[:-1].split('/') if info.is_dir() else name.split('/')
            if (not name or len(name) > 240 or '\\' in name or name.startswith('/') or
                    ':' in name or any(part in ('', '.', '..') for part in parts) or
                    mode == 0o120000 or info.flag_bits & 1 or
                    info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)):
                finding(report, 'error', 'unsafe_entry', f'不安全的归档条目：{name[:120]}')
            folded = name.casefold()
            if folded in names:
                finding(report, 'error', 'duplicate_entry', f'重复的文件名：{name[:120]}')
            names.add(folded)
            if len(path.parts) > 1:
                roots.add(path.parts[0])
            elif name not in {'README.txt', 'SHA256SUMS.txt'} and not info.is_dir():
                finding(report, 'error', 'unexpected_root_file', f'根目录有意外文件：{name[:120]}')
            if path.suffix.lower() in SENSITIVE_SUFFIXES or path.name.lower() in {'.env', 'id_rsa'}:
                finding(report, 'error', 'sensitive_file', f'包含密钥或可执行文件：{name[:120]}')
        if len(roots) != 1 or not SLUG.fullmatch(next(iter(roots), '')):
            finding(report, 'error', 'project_root', 'ZIP 应只有一个小写英文项目目录')
        if any(f['level'] == 'error' for f in report['findings']):
            return finish(report)
        root = next(iter(roots))
        unexpected_wasm = [info.filename for info in infos
                           if info.filename.lower().endswith('.wasm')
                           and info.filename != f'{root}/{root}.wasm']
        if unexpected_wasm:
            finding(report, 'error', 'extra_wasm',
                    f'投稿包含额外 WASM：{unexpected_wasm[0][:120]}')
            return finish(report)
        try:
            files = {info.filename: archive.read(info) for info in infos}
        except (zipfile.BadZipFile, RuntimeError, OSError, EOFError) as exc:
            finding(report, 'error', 'archive_integrity', f'归档内容或 CRC 损坏：{exc}')
            return finish(report)
    finding(report, 'pass', 'archive', f'ZIP 结构安全；{len(files)} 个文件，未执行或解压源码')
    manifest_path, wasm_path = f'{root}/manifest.json', f'{root}/{root}.wasm'
    if manifest_path not in files or wasm_path not in files:
        finding(report, 'error', 'required_files', f'必须包含 {manifest_path} 和 {wasm_path}')
        return finish(report)
    if 'SHA256SUMS.txt' not in files:
        finding(report, 'error', 'checksum_missing', '缺少 SHA256SUMS.txt')
    try:
        manifest = json.loads(files[manifest_path].decode('utf-8'), object_pairs_hook=unique_keys)
        prepare_candidate(manifest, files[wasm_path])
    except (UnicodeError, ValueError, TypeError) as exc:
        finding(report, 'error', 'manifest_or_wasm', f'Manifest/WASM 不符合打包规则：{exc}')
        return finish(report)
    report['manifest'] = {key: manifest.get(key) for key in
                          ('id', 'name', 'version', 'platform_min', 'author', 'description',
                           'permissions', 'hardware', 'optional_hardware')}
    finding(report, 'pass', 'manifest', f'{manifest["id"]} {manifest["version"]}；WASM {len(files[wasm_path])} 字节')
    digest = hashlib.sha256(files[wasm_path]).hexdigest()
    if 'SHA256SUMS.txt' in files:
        try:
            match = SHA_LINE.fullmatch(files['SHA256SUMS.txt'].decode('ascii'))
            if not match or match.group(1) != digest or match.group(2) != wasm_path:
                raise ValueError('WASM 哈希或路径不匹配')
            finding(report, 'pass', 'checksum', f'WASM SHA256：{digest}')
        except (UnicodeError, ValueError) as exc:
            finding(report, 'error', 'checksum', f'SHA256SUMS.txt 无效：{exc}')
    source = {f'{root}/main.c', f'{root}/main.cpp', f'{root}/main.cc'} & files.keys()
    rust = f'{root}/Cargo.toml' in files and f'{root}/src/lib.rs' in files
    if not source and not rust:
        finding(report, 'error', 'source_missing', '缺少可审核的 C/C++ 或 Rust 入口源码')
    else:
        finding(report, 'pass', 'source', '包含应用入口源码；源码与 WASM 是否一致仍需可信环境重建')
    if not manifest.get('author') or manifest['author'].strip() in {'未填写', '待填写'}:
        finding(report, 'warning', 'author', 'Manifest 未填写公开作者名，详情页会显示“未填写”')
    if not manifest.get('description'):
        finding(report, 'warning', 'description', 'Manifest 未填写应用简介')
    info = files.get(f'{root}/投稿信息.md')
    if not info:
        finding(report, 'warning', 'submission_info', '缺少投稿信息.md；需核对作者联系、权限用途与素材授权')
    else:
        text = info.decode('utf-8', errors='replace')
        if re.search(r'待投稿人填写|待填写|联系方式：\s*(?:\n|$)|尚未完成', text):
            finding(report, 'warning', 'submission_info_incomplete', '投稿信息仍有待填写或待验收内容')
        else:
            finding(report, 'pass', 'submission_info', '已附投稿信息；内容真实性仍需人工核对')
    imports = inspect_imports(files[wasm_path], report)
    if imports is not None:
        check_capabilities(manifest, imports, report)
    finding(report, 'warning', 'manual_gates',
            '自动初审不能证明源码安全、授权有效、作者归属或真机行为；通过后仍需重建、实机测试与人工批准')
    return finish(report)


def markdown(report):
    labels = {'error': '失败', 'warning': '待复核', 'pass': '通过'}
    lines = [f'# 投稿包自动初审：{report["file"]}', '',
             f'结论：**{report["status"]}**（REJECTED=拒收；MANUAL_REVIEW=进入人工复核）',
             f'检查时间：{report["checked_at"]}', f'ZIP SHA256：`{report["zip_sha256"]}`', '']
    if report['manifest']:
        m = report['manifest']
        lines += [f'应用：{m["name"]} · `{m["id"]}` · {m["version"]}', '']
    lines += ['## 检查结果', '']
    lines += [f'- **{labels[item["level"]]}** `{item["code"]}`：{item["detail"]}'
              for item in report['findings']]
    lines += ['', '此报告不签名、不发布应用；人工批准前不得放入正式目录。', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zip', type=Path, help='Developer Studio submission ZIP')
    parser.add_argument('--output', type=Path, help='Report directory; default: local/submission-reviews')
    args = parser.parse_args()
    report = audit_bytes(args.zip.read_bytes(), args.zip.name)
    output = args.output or ROOT / 'local/submission-reviews'
    output.mkdir(parents=True, exist_ok=True)
    prefix = re.sub(r'[^a-zA-Z0-9_-]+', '_', args.zip.stem)[:100]
    json_path, md_path = output / (prefix + '-review.json'), output / (prefix + '-review.md')
    json_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    md_path.write_text(markdown(report), encoding='utf-8')
    print(f'{report["status"]}: {report["counts"]}')
    print(f'Reports: {md_path} | {json_path}')
    return 2 if report['status'] == 'REJECTED' else 0


if __name__ == '__main__':
    sys.exit(main())

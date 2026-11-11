"""Local-only developer workbench for unsigned ESP Mini App submissions."""
import argparse
from email import policy
from email.parser import BytesParser
import hashlib
import importlib.metadata
import io
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import threading
from datetime import datetime, timezone
import webbrowser
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
from urllib.parse import parse_qs, unquote, urlsplit

from node_runtime import node_executable

ROOT = Path(__file__).resolve().parents[1]
PAGES = {
    '/': 'developer_studio.html',
    '/project': 'studio_project.html',
    '/preview': 'studio_preview.html',
    '/app': 'studio_app.html',
    '/examples': 'studio_examples.html',
    '/docs': 'studio_docs.html',
    '/review': 'studio_review.html',
}
ASSETS = {'/studio.css': 'studio.css', '/studio.js': 'developer_studio.js',
          '/home.js': 'studio_home.js', '/project.js': 'studio_project.js',
          '/preview.js': 'studio_preview.js', '/app.js': 'studio_app.js',
          '/examples.js': 'studio_examples.js', '/docs.js': 'studio_docs.js',
          '/review.js': 'studio_review.js'}
SLUG = re.compile(r'[a-z][a-z0-9_-]{1,39}\Z')
SKIP_DIRS = {'.git', '.build', '.target', 'target', '__pycache__', 'node_modules', '.venv'}
SKIP_NAMES = {'.DS_Store', 'dev_private.pem'}
SKIP_SUFFIXES = {'.pem', '.key', '.p12', '.pfx', '.app', '.wasm', '.exe', '.dll', '.obj', '.o', '.pyc', '.zip'}
SOURCE_SUFFIXES = {'.c', '.cc', '.cpp', '.h', '.hpp', '.rs', '.toml', '.lock', '.json',
                   '.md', '.txt', '.png', '.jpg', '.jpeg', '.svg', '.webp', '.wav', '.ogg',
                   '.ttf', '.otf', '.inc'}
MAX_SOURCE = 10 * 1024 * 1024
MAX_EDIT_FILE = 256 * 1024
EDIT_SUFFIXES = {'.c', '.cc', '.cpp', '.h', '.hpp', '.rs', '.toml', '.lock', '.json',
                 '.md', '.txt', '.inc'}
DOCS = {
    'api': ('平台函数速查', 'docs/PLATFORM_API.md'),
    'first_batch': ('第一批开放能力（0.5.0）', 'docs/FIRST_BATCH_CAPABILITIES.md'),
    'guide': ('第三方应用代码规范与构建指南', 'docs/THIRD_PARTY_CODE_GUIDE.md'),
    'examples': ('示例应用目录', 'examples/README.md'),
    'sdk': ('SDK 与构建说明', 'docs/SDK.md'),
    'format': ('应用格式与 Manifest', 'docs/app_format.md'),
    'hardware': ('外设需求与能力', 'docs/HARDWARE_REQUIREMENTS.md'),
    'submission': ('第三方应用投稿流程', 'docs/THIRD_PARTY_APPS.md'),
}
EXAMPLES = {
    'c_first_batch': {
        'title': '第一批能力演示', 'language': 'C', 'directory': 'examples/c_first_batch',
        'summary': '触摸手势、中文和图片，以及可选的传感器、电量和反馈。',
        'features': ['触摸按下、移动、长按、滑动', 'GB2312 中文与 RGB565 图片', 'IMU/姿态、磁场、电量与震动/LED 按需降级'],
        'try': '构建并预览，点击、长按或滑动画布；调整预览传感器值和模拟外设。',
        'files': ['main.c', 'manifest.json', 'artwork.h'],
    },
    'c_color_pad': {
        'title': '触摸调色板', 'language': 'C', 'directory': 'examples/c_color_pad',
        'summary': '点选四块颜色，学习应用入口、触摸释放事件和即时绘图。',
        'features': ['启动时绘制四块颜色和选中标记', '触摸某块颜色后更新选择', '用 clear、rect、text 组合画面'],
        'try': '构建并预览后，点击任意色块；白色标记会移动到所选色块。',
        'files': ['main.c', 'manifest.json'],
    },
    'c_scene_timer': {
        'title': '场景计时器', 'language': 'C', 'directory': 'examples/c_scene_timer',
        'summary': '每秒更新计时和进度条，学习完整场景提交与毫秒计时回绕。',
        'features': ['50 ms 定时事件驱动秒表', '一次 platform_scene 提交完整画面', '触摸切换暂停和继续'],
        'try': '构建并预览后观察数字和进度条；点击屏幕可暂停或继续。',
        'files': ['main.c', 'manifest.json'],
    },
    'c_optional_input': {
        'title': '可选输入演示', 'language': 'C', 'directory': 'examples/c_optional_input',
        'summary': '人体感应和实体按键不存在时仍可用触摸操作。',
        'features': ['platform_hardware 查询当前能力', '区分必需与可选外设', '缺少按键或人体感应时使用触摸降级'],
        'try': '预览页切换“仅屏幕和触摸”，再点击画布；输入计数仍会增加。',
        'files': ['main.c', 'manifest.json'],
    },
    'rust_saved_counter': {
        'title': '持久计数器', 'language': 'Rust', 'directory': 'examples/rust_saved_counter',
        'summary': '触摸增加计数并保存到应用专属键值空间。',
        'features': ['no_std Rust 应用入口', 'kv_read / kv_write 持久化', '检查写入返回值并提示延后重试'],
        'try': '构建并预览后反复点击画布，观察计数和保存状态。电脑预览只模拟存储。',
        'files': ['src/lib.rs', 'Cargo.toml', 'manifest.json'],
    },
    'cpp_bounce': {
        'title': '弹跳方块', 'language': 'C++', 'directory': 'apps/bounce',
        'summary': '方块在画布边界反弹，演示 C++ 类、定时事件和完整场景。',
        'features': ['局部 C++ 类与 constexpr', '约每 40 ms 更新方块位置', '触摸重新放置方块'],
        'try': '构建并预览后观察方块反弹；点击画布可改变方块位置。',
        'files': ['main.cpp', 'manifest.json'],
    },
    'c_template': {
        'title': '最简应用模板', 'language': 'C', 'directory': 'templates/third_party_app',
        'summary': '从一个最小可构建应用开始，理解 Manifest、启动入口和触摸事件。',
        'features': ['app_init 绘制初始画面', 'app_event 接收触摸释放', 'Manifest 声明显示与触摸能力'],
        'try': '复制到项目后修改名称和应用 ID，构建预览，再点击画布。',
        'files': ['main.c', 'manifest.json'],
    },
}


def allowed_source(rel):
    return (not any(part in SKIP_DIRS or part.startswith('.') for part in rel.parts)
            and rel.name not in SKIP_NAMES and rel.suffix.lower() not in SKIP_SUFFIXES
            and (rel.suffix.lower() in SOURCE_SUFFIXES
                 or rel.name.upper() in {'LICENSE', 'COPYING', 'NOTICE'}))


def project_files(project):
    """Yield ordinary source files, rejecting links and unusually large trees."""
    total = 0
    for path in sorted(project.rglob('*')):
        rel = path.relative_to(project)
        if path.is_symlink():
            raise ValueError(f'symbolic link is not supported: {rel}')
        if not path.is_file() or not allowed_source(rel):
            continue
        size = path.stat().st_size
        total += size
        if total > MAX_SOURCE:
            raise ValueError('source files exceed 10 MiB')
        yield path, rel


class Studio:
    def __init__(self, workspace):
        self.workspace = workspace.resolve()
        self.projects = self.workspace / 'projects'
        self.builds = self.workspace / 'builds'
        self.exports = self.workspace / 'exports'
        self.removed = self.workspace / 'removed-projects'
        for path in (self.projects, self.builds, self.exports, self.removed):
            path.mkdir(parents=True, exist_ok=True)
            if path.is_symlink() or not path.resolve().is_relative_to(self.workspace):
                raise ValueError('workspace directory must not be a link outside the workspace')
        self.token = secrets.token_urlsafe(32)
        self.lock = threading.Lock()

    def path(self, name):
        if not isinstance(name, str) or not SLUG.fullmatch(name):
            raise ValueError('project name must use lowercase letters, digits, _ or -')
        path = self.projects / name
        if path.is_symlink():
            raise ValueError('linked project is not supported')
        return path

    def summary(self, name):
        p = self.path(name)
        m = json.loads((p / 'manifest.json').read_text(encoding='utf-8'))
        language = 'rust' if (p / 'Cargo.toml').is_file() else 'c++' if any((p / x).is_file() for x in ('main.cpp', 'main.cc')) else 'c'
        return {'project': name, 'manifest': m, 'language': language,
                'built': (self.builds / name / (name + '.wasm')).is_file()}

    def list(self):
        result = []
        for p in sorted(self.projects.iterdir()):
            if p.is_dir() and not p.is_symlink() and SLUG.fullmatch(p.name) and (p / 'manifest.json').is_file():
                try:
                    result.append(self.summary(p.name))
                except (ValueError, OSError, json.JSONDecodeError):
                    pass
        return result

    @staticmethod
    def docs():
        return [{'id': key, 'title': title, 'path': path} for key, (title, path) in DOCS.items()]

    @staticmethod
    def document(key):
        if key not in DOCS:
            raise ValueError('unknown document')
        title, relative = DOCS[key]
        return {'id': key, 'title': title, 'path': relative,
                'markdown': (ROOT / relative).read_text(encoding='utf-8')}

    @staticmethod
    def examples():
        return [{'id': key, **{field: value for field, value in item.items() if field != 'files'}}
                for key, item in EXAMPLES.items()]

    @staticmethod
    def example(key):
        if key not in EXAMPLES:
            raise ValueError('unknown example')
        item = EXAMPLES[key]
        directory = ROOT / item['directory']
        files = [{'path': relative, 'content': (directory / relative).read_text(encoding='utf-8')}
                 for relative in item['files']]
        return {'id': key, **{field: value for field, value in item.items() if field != 'files'},
                'files': files}

    def copy_example(self, key):
        if key not in EXAMPLES:
            raise ValueError('unknown example')
        source = ROOT / EXAMPLES[key]['directory']
        return self.import_project(self.available_import_name(key if SLUG.fullmatch(key) else 'example_' + key), str(source))

    def removed_list(self):
        result = []
        for entry in self.removed.iterdir():
            if entry.is_symlink() or not entry.is_dir() or not re.fullmatch(r'[0-9a-f]{16}', entry.name):
                continue
            try:
                metadata = json.loads((entry / 'meta.json').read_text(encoding='utf-8'))
                if SLUG.fullmatch(metadata['name']) and (entry / 'project').is_dir():
                    result.append({'id': entry.name, 'name': metadata['name'],
                                   'title': metadata['title'], 'removed_at': metadata['removed_at']})
            except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError):
                continue
        return sorted(result, key=lambda item: item['removed_at'], reverse=True)

    def remove_project(self, name):
        project = self.path(name)
        if not project.is_dir() or not (project / 'manifest.json').is_file():
            raise ValueError('project does not exist')
        if not project.resolve().is_relative_to(self.projects.resolve()):
            raise ValueError('project path is outside the workspace')
        built = self.builds / name
        if built.is_symlink() or (built.exists() and not built.resolve().is_relative_to(self.builds.resolve())):
            raise ValueError('build path is outside the workspace')
        if built.exists() and not built.is_dir():
            raise ValueError('build path is not a directory')
        entry = self.removed / secrets.token_hex(8)
        if not entry.resolve().is_relative_to(self.workspace):
            raise ValueError('archive path is outside the workspace')
        title = self.summary(name)['manifest']['name']
        entry.mkdir()
        moved_project = False
        moved_build = False
        try:
            project.replace(entry / 'project')
            moved_project = True
            if built.is_dir():
                built.replace(entry / 'build')
                moved_build = True
            metadata = {'name': name, 'title': title,
                        'removed_at': datetime.now(timezone.utc).isoformat()}
            (entry / 'meta.json').write_text(json.dumps(metadata, ensure_ascii=False), encoding='utf-8')
        except Exception:
            if moved_build:
                (entry / 'build').replace(built)
            if moved_project:
                (entry / 'project').replace(project)
            (entry / 'meta.json').unlink(missing_ok=True)
            entry.rmdir()
            raise
        return {'id': entry.name, **metadata}

    def restore_project(self, archive_id):
        if not isinstance(archive_id, str) or not re.fullmatch(r'[0-9a-f]{16}', archive_id):
            raise ValueError('invalid archive id')
        entry = self.removed / archive_id
        if entry.is_symlink() or not entry.is_dir() or not entry.resolve().is_relative_to(self.removed.resolve()):
            raise ValueError('removed project does not exist')
        metadata = json.loads((entry / 'meta.json').read_text(encoding='utf-8'))
        name = metadata['name']
        project = self.path(name)
        built = self.builds / name
        if project.exists() or built.exists() or project.is_symlink() or built.is_symlink():
            raise ValueError('同名项目或构建目录已存在；请先移除冲突项目')
        source = entry / 'project'
        if source.is_symlink() or not source.is_dir() or not source.resolve().is_relative_to(entry.resolve()):
            raise ValueError('removed project is invalid')
        archived_build = entry / 'build'
        if archived_build.is_symlink() or (archived_build.exists() and not archived_build.resolve().is_relative_to(entry.resolve())):
            raise ValueError('removed build is invalid')
        source.replace(project)
        try:
            if archived_build.is_dir():
                archived_build.replace(built)
        except Exception:
            project.replace(source)
            raise
        (entry / 'meta.json').unlink()
        entry.rmdir()
        return self.summary(name)

    def create(self, name):
        p = self.path(name)
        if p.exists():
            raise ValueError('project already exists')
        shutil.copytree(ROOT / 'templates' / 'third_party_app', p)
        return self.summary(name)

    def available_import_name(self, name):
        """Keep imports as copies even when the requested directory already exists."""
        self.path(name)
        if not (self.path(name).exists() or self.path(name).is_symlink()):
            return name
        for number in range(2, 10001):
            suffix = f'_{number}'
            candidate = name[:40 - len(suffix)] + suffix
            path = self.path(candidate)
            if not (path.exists() or path.is_symlink()):
                return candidate
        raise ValueError('no free project name for this import')

    def import_project(self, name, source):
        self.path(name)
        if not isinstance(source, str) or not source.strip():
            raise ValueError('source folder is required')
        source = Path(source).expanduser().resolve(strict=True)
        if not source.is_dir() or not (source / 'manifest.json').is_file():
            raise ValueError('source folder must contain manifest.json')
        files = list(project_files(source))
        if not any(rel.as_posix() == 'manifest.json' for _, rel in files):
            raise ValueError('manifest.json is required')
        name = self.available_import_name(name)
        p = self.path(name)
        created = False
        try:
            p.mkdir()
            created = True
            for file, rel in files:
                target = p / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(file, target)
            self.prepare_rust_dependency(p)
            self.summary(name)
        except Exception:
            if created:
                shutil.rmtree(p, ignore_errors=True)
            raise
        return self.summary(name)

    def import_files(self, name, paths, contents):
        """Import a folder selected in the browser; paths are relative to that folder."""
        self.path(name)
        if not isinstance(paths, list) or not 1 <= len(paths) <= 500 or len(paths) != len(contents):
            raise ValueError('invalid folder upload')
        root_name = None
        accepted = []
        total = 0
        seen = set()
        for raw, data in zip(paths, contents):
            if not isinstance(raw, str) or '\\' in raw or '\x00' in raw or ':' in raw or raw.startswith('/'):
                raise ValueError('invalid relative file path')
            parts = PurePosixPath(raw).parts
            if (len(parts) < 2 or any(part in ('', '.', '..') or part.endswith((' ', '.'))
                                      or any(char in part for char in '<>"|?*') for part in parts)):
                raise ValueError('invalid relative file path')
            if root_name is None:
                root_name = parts[0]
            if parts[0] != root_name:
                raise ValueError('select one project folder')
            rel = Path(*parts[1:])
            if rel in seen:
                raise ValueError(f'duplicate file: {rel}')
            seen.add(rel)
            if not allowed_source(rel):
                continue
            total += len(data)
            if total > MAX_SOURCE:
                raise ValueError('source files exceed 10 MiB')
            accepted.append((rel, data))
        if not any(rel == Path('manifest.json') for rel, _ in accepted):
            raise ValueError('所选文件夹根目录必须包含 manifest.json')
        with tempfile.TemporaryDirectory(prefix='studio-import-', dir=self.workspace) as folder:
            source = Path(folder)
            for rel, data in accepted:
                target = source / rel
                if not target.resolve().is_relative_to(source.resolve()):
                    raise ValueError('file path escapes selected folder')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            return self.import_project(name, str(source))

    @staticmethod
    def prepare_rust_dependency(project):
        """Make the bundled miniapp SDK path portable in imported Rust examples."""
        cargo = project / 'Cargo.toml'
        if not cargo.is_file():
            return
        content = cargo.read_text(encoding='utf-8')
        dependency = re.search(r'miniapp\s*=\s*\{[^}]*\bpath\s*=\s*"[^"]+"[^}]*\}', content)
        if not dependency:
            return
        vendor = project / 'vendor' / 'miniapp'
        if not vendor.exists():
            shutil.copytree(ROOT / 'sdk/rust/miniapp', vendor)
        rewritten = re.sub(r'\bpath\s*=\s*"[^"]+"', 'path = "vendor/miniapp"', dependency.group())
        cargo.write_text(content[:dependency.start()] + rewritten + content[dependency.end():], encoding='utf-8')

    def save_manifest(self, name, manifest):
        from app_package import validate_manifest
        p = self.path(name)
        if not p.is_dir():
            raise ValueError('project does not exist')
        if not isinstance(manifest, dict):
            raise ValueError('invalid manifest')
        old = json.loads((p / 'manifest.json').read_text(encoding='utf-8'))
        for field in ('manifest_version', 'api_version', 'entry'):
            manifest[field] = old.get(field)
        for field in ('wasm_size', 'sha256'):
            manifest.pop(field, None)
        validate_manifest(manifest)
        data = json.dumps(manifest, ensure_ascii=False, indent=2) + '\n'
        if len(data.encode('utf-8')) > 2048:
            raise ValueError('manifest exceeds 2048 bytes')
        (p / 'manifest.json').write_text(data, encoding='utf-8')
        # A changed manifest invalidates the last verified build.
        wasm = self.builds / name / (name + '.wasm')
        if wasm.exists():
            wasm.unlink()
        return self.summary(name)

    def editable_path(self, name, relative):
        project = self.path(name)
        if not project.is_dir() or not isinstance(relative, str) or '\\' in relative or '\x00' in relative:
            raise ValueError('invalid project file')
        parts = PurePosixPath(relative).parts
        if (not parts or relative.startswith('/') or any(part in ('', '.', '..') for part in parts)
                or ':' in relative):
            raise ValueError('invalid project file')
        rel = Path(*parts)
        if rel.as_posix() == 'manifest.json' or rel.suffix.lower() not in EDIT_SUFFIXES or not allowed_source(rel):
            raise ValueError('use the Manifest form or an external editor for this file')
        path = project / rel
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(project.resolve()):
            raise ValueError('project file does not exist')
        if path.stat().st_size > MAX_EDIT_FILE:
            raise ValueError('file exceeds the browser editor limit (256 KiB)')
        return path

    def edit_files(self, name):
        project = self.path(name)
        if not project.is_dir():
            raise ValueError('project does not exist')
        return [rel.as_posix() for path, rel in project_files(project)
                if rel.as_posix() != 'manifest.json' and rel.suffix.lower() in EDIT_SUFFIXES
                and path.stat().st_size <= MAX_EDIT_FILE]

    def read_file(self, name, relative):
        data = self.editable_path(name, relative).read_bytes()
        try:
            content = data.decode('utf-8')
        except UnicodeDecodeError as error:
            raise ValueError('file is not UTF-8 text; use an external editor') from error
        return {'path': relative, 'content': content,
                'sha256': hashlib.sha256(data).hexdigest()}

    def save_file(self, name, relative, content, expected_sha256):
        path = self.editable_path(name, relative)
        if not isinstance(content, str) or not isinstance(expected_sha256, str):
            raise ValueError('invalid editor content')
        data = content.encode('utf-8')
        if len(data) > MAX_EDIT_FILE:
            raise ValueError('file exceeds the browser editor limit (256 KiB)')
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected_sha256:
            raise ValueError('文件已被其他编辑器修改；请重新打开并检查后再保存')
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.studio-', delete=False) as temp:
            temp.write(data)
            temporary = Path(temp.name)
        try:
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
        (self.builds / name / (name + '.wasm')).unlink(missing_ok=True)
        return {'path': relative, 'sha256': hashlib.sha256(data).hexdigest()}

    def diagnostics(self):
        checks = []
        def add(name, ok, detail, fix=''):
            checks.append({'name': name, 'ok': ok, 'detail': detail, 'fix': fix})
        add('Python', True, sys.version.split()[0])
        try:
            version = importlib.metadata.version('cryptography')
            add('Python cryptography', True, version)
        except importlib.metadata.PackageNotFoundError:
            add('Python cryptography', False, '未安装', 'python -m pip install -r requirements-app-sdk.txt')
        try:
            node = node_executable()
            add('Node.js（ABI 检查）', True, node)
        except FileNotFoundError:
            add('Node.js（ABI 检查）', False, '未找到 node.exe', '安装 Node.js，或设置 NODE_EXE 为 node.exe 的绝对路径后重启工作台')
        sdk = Path(os.environ.get('WASI_SDK_PATH', 'C:/esp/mini-app-sdk/wasi-sdk-34.0-x86_64-windows'))
        compiler = sdk / 'bin' / ('clang.exe' if os.name == 'nt' else 'clang')
        add('C/C++ WASI SDK', compiler.is_file(), str(compiler),
            './tools/setup_sdk.ps1；自定义路径可设置 WASI_SDK_PATH 后重启工作台')
        cargo = shutil.which('cargo') or str(Path.home() / '.cargo/bin' / ('cargo.exe' if os.name == 'nt' else 'cargo'))
        cargo_ok = Path(cargo).is_file()
        add('Rust cargo（仅 Rust 项目）', cargo_ok, cargo if cargo_ok else '未找到',
            '安装 rustup 后重启工作台')
        rustup = shutil.which('rustup') or str(Path.home() / '.cargo/bin' / ('rustup.exe' if os.name == 'nt' else 'rustup'))
        if Path(rustup).is_file():
            try:
                run = subprocess.run([rustup, 'target', 'list', '--installed'], capture_output=True,
                                     text=True, timeout=10)
                target_ok = run.returncode == 0 and 'wasm32-unknown-unknown' in run.stdout.splitlines()
            except (OSError, subprocess.TimeoutExpired):
                target_ok = False
        else:
            target_ok = False
        add('Rust WASM target（仅 Rust 项目）', target_ok,
            'wasm32-unknown-unknown' if target_ok else '未安装或无法查询',
            'rustup target add wasm32-unknown-unknown')
        return checks

    def build(self, name):
        p = self.path(name)
        if not p.is_dir():
            raise ValueError('project does not exist')
        out = self.builds / name
        out.mkdir(parents=True, exist_ok=True)
        wasm = out / (name + '.wasm')
        if wasm.exists():
            wasm.unlink()
        rust = (p / 'Cargo.toml').is_file()
        script = 'build_rust_app.py' if rust else 'build_c_app.py'
        cmd = [sys.executable, str(ROOT / 'tools' / script), str(p), '--unsigned', '--output', str(out)]
        if rust:
            cmd += ['--target-dir', str(self.workspace / '.target' / name)]
        run = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors='replace', timeout=180,
                             env={**os.environ, 'PYTHONUNBUFFERED': '1'})
        log = '$ ' + ' '.join(cmd) + '\n' + run.stdout + run.stderr
        if run.returncode or not wasm.is_file():
            return {'ok': False, 'log': log[-30000:]}
        checked = subprocess.run([sys.executable, str(ROOT / 'tools/check_submission.py'), str(p), str(wasm)],
                                 cwd=ROOT, capture_output=True, text=True, errors='replace', timeout=30)
        log += '\n$ check_submission.py\n' + checked.stdout + checked.stderr
        if checked.returncode:
            wasm.unlink(missing_ok=True)
        return {'ok': checked.returncode == 0, 'log': log[-30000:]}

    def verified(self, name):
        from app_package import prepare_candidate
        p = self.path(name)
        wasm = self.builds / name / (name + '.wasm')
        if not p.is_dir() or not wasm.is_file():
            raise ValueError('build the project before preview or export')
        m = json.loads((p / 'manifest.json').read_text(encoding='utf-8'))
        data = wasm.read_bytes()
        prepare_candidate(m, data)
        source_mtime = max((f.stat().st_mtime_ns for f, _ in project_files(p)), default=0)
        if source_mtime > wasm.stat().st_mtime_ns:
            raise ValueError('source changed after the build; rebuild first')
        checked = subprocess.run([sys.executable, str(ROOT / 'tools/check_submission.py'), str(p), str(wasm)],
                                 cwd=ROOT, capture_output=True, text=True, errors='replace', timeout=30)
        if checked.returncode:
            raise ValueError('submission check failed; rebuild first: ' + (checked.stderr or checked.stdout)[-1000:])
        return m, data

    def export(self, name):
        m, wasm = self.verified(name)
        p = self.path(name)
        digest = hashlib.sha256(wasm).hexdigest()
        output = self.exports / f'{name}-{m["version"]}-submission.zip'
        with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
            for source, rel in project_files(p):
                archive.write(source, f'{name}/{rel.as_posix()}')
            archive.writestr(f'{name}/{name}.wasm', wasm)
            archive.writestr('SHA256SUMS.txt', f'{digest}  {name}/{name}.wasm\n')
            archive.writestr('README.txt', 'Unsigned submission for manual review. This is not an installable .app.\n'
                             'The platform maintainer must review source and licenses, rebuild, test, and sign.\n')
        return output


def handler_for(studio):
    class Handler(BaseHTTPRequestHandler):
        def reply(self, status, data, mime='application/json; charset=utf-8', headers=None):
            if isinstance(data, (dict, list)):
                data = json.dumps(data, ensure_ascii=False).encode('utf-8')
            elif isinstance(data, str):
                data = data.encode('utf-8')
            self.send_response(status)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            # Browser previews instantiate WASM; permit that operation without
            # enabling general JavaScript eval or scripts from other origins.
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; object-src 'none'; frame-ancestors 'none'")
            for key, value in (headers or {}).items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(data)

        def allowed(self):
            host = self.headers.get('Host', '')
            origin = self.headers.get('Origin', '')
            return host in (f'127.0.0.1:{self.server.server_port}', f'localhost:{self.server.server_port}') and (not origin or origin == f'http://{host}')

        def do_GET(self):
            if not self.allowed():
                return self.reply(403, {'error': 'invalid host or origin'})
            url = urlsplit(self.path)
            query = parse_qs(url.query)
            try:
                if url.path in PAGES:
                    return self.reply(200, (ROOT / 'tools' / PAGES[url.path]).read_bytes(), 'text/html; charset=utf-8')
                if url.path in ASSETS:
                    mime = 'text/css; charset=utf-8' if url.path.endswith('.css') else 'text/javascript; charset=utf-8'
                    return self.reply(200, (ROOT / 'tools' / ASSETS[url.path]).read_bytes(), mime)
                if url.path == '/api/session':
                    return self.reply(200, {'token': studio.token, 'workspace': str(studio.workspace),
                                            'capabilities': ['folder_import_v1', 'editor_v1', 'diagnostics_v1',
                                                             'remove_restore_v1', 'docs_v1', 'pages_v1',
                                                             'examples_v1', 'submission_review_v1']})
                if url.path in ('/api/diagnostics', '/api/files', '/api/file',
                                '/api/removed', '/api/docs', '/api/doc', '/api/examples', '/api/example'):
                    if self.headers.get('X-Studio-Token') != studio.token:
                        return self.reply(403, {'error': 'invalid session token'})
                    if url.path == '/api/diagnostics':
                        return self.reply(200, studio.diagnostics())
                    if url.path == '/api/removed':
                        return self.reply(200, studio.removed_list())
                    if url.path == '/api/docs':
                        return self.reply(200, studio.docs())
                    if url.path == '/api/doc':
                        return self.reply(200, studio.document(query.get('id', [''])[0]))
                    if url.path == '/api/examples':
                        return self.reply(200, studio.examples())
                    if url.path == '/api/example':
                        return self.reply(200, studio.example(query.get('id', [''])[0]))
                    name = query.get('name', [''])[0]
                    if url.path == '/api/files':
                        return self.reply(200, studio.edit_files(name))
                    return self.reply(200, studio.read_file(name, query.get('path', [''])[0]))
                if url.path == '/api/projects':
                    return self.reply(200, studio.list())
                if url.path in ('/api/project', '/api/wasm', '/api/export'):
                    name = query.get('name', [''])[0]
                    if url.path == '/api/project':
                        return self.reply(200, studio.summary(name))
                    if self.headers.get('X-Studio-Token') != studio.token:
                        return self.reply(403, {'error': 'invalid session token'})
                    if url.path == '/api/wasm':
                        _, wasm = studio.verified(name)
                        return self.reply(200, wasm, 'application/wasm')
                    output = studio.export(name)
                    return self.reply(200, output.read_bytes(), 'application/zip',
                                      {'Content-Disposition': f'attachment; filename="{output.name}"'})
                self.reply(404, {'error': 'not found'})
            except (ValueError, OSError, ImportError, json.JSONDecodeError) as e:
                self.reply(400, {'error': str(e)})

        def do_POST(self):
            if not self.allowed() or self.headers.get('X-Studio-Token') != studio.token:
                return self.reply(403, {'error': 'invalid session'})
            try:
                length = int(self.headers.get('Content-Length', '0'))
                upload = self.path == '/api/import-files'
                review = self.path == '/api/review-upload'
                limit = (MAX_SOURCE + 2 * 1024 * 1024 if upload else
                         12 * 1024 * 1024 if review else
                         MAX_EDIT_FILE * 4 if self.path == '/api/file' else 16384)
                if not 0 < length <= limit:
                    raise ValueError('request is too large')
                content_type = self.headers.get('Content-Type', '')
                body = self.rfile.read(length)
                if review:
                    if content_type.split(';')[0] not in ('application/zip', 'application/octet-stream'):
                        raise ValueError('expected a ZIP file')
                    from review_submission import audit_bytes
                    filename = unquote(self.headers.get('X-Submission-Name', 'submission.zip'))
                    return self.reply(200, audit_bytes(body, filename))
                if upload:
                    if not re.fullmatch(r'multipart/form-data;\s*boundary=[A-Za-z0-9-]{1,70}', content_type):
                        raise ValueError('expected folder upload')
                    message = BytesParser(policy=policy.default).parsebytes(
                        b'Content-Type: ' + content_type.encode('ascii') + b'\r\nMIME-Version: 1.0\r\n\r\n' + body)
                    parts = list(message.iter_parts())
                    if not message.is_multipart() or not 2 <= len(parts) <= 501:
                        raise ValueError('invalid folder upload')
                    fields = {}
                    for part in parts:
                        if part.get_content_disposition() != 'form-data':
                            raise ValueError('invalid form field')
                        key = part.get_param('name', header='content-disposition')
                        if not isinstance(key, str) or key in fields:
                            raise ValueError('duplicate or missing form field')
                        fields[key] = part.get_payload(decode=True)
                    meta = json.loads(fields.pop('meta').decode('utf-8'))
                    paths = meta['paths']
                    if set(fields) != {str(i) for i in range(len(paths))}:
                        raise ValueError('file list does not match paths')
                    with studio.lock:
                        result = studio.import_files(meta['name'], paths,
                                                     [fields[str(i)] for i in range(len(paths))])
                    return self.reply(200, result)
                if content_type.split(';')[0] != 'application/json':
                    return self.reply(415, {'error': 'expected JSON'})
                payload = json.loads(body)
                if not isinstance(payload, dict):
                    raise ValueError('expected object')
                with studio.lock:
                    if self.path == '/api/create':
                        result = studio.create(payload.get('name'))
                    elif self.path == '/api/import':
                        result = studio.import_project(payload.get('name'), payload.get('source'))
                    elif self.path == '/api/manifest':
                        result = studio.save_manifest(payload.get('name'), payload.get('manifest'))
                    elif self.path == '/api/build':
                        result = studio.build(payload.get('name'))
                    elif self.path == '/api/file':
                        result = studio.save_file(payload.get('name'), payload.get('path'),
                                                  payload.get('content'), payload.get('sha256'))
                    elif self.path == '/api/remove':
                        result = studio.remove_project(payload.get('name'))
                    elif self.path == '/api/restore':
                        result = studio.restore_project(payload.get('id'))
                    elif self.path == '/api/example-copy':
                        result = studio.copy_example(payload.get('id'))
                    else:
                        return self.reply(404, {'error': 'not found'})
                self.reply(200, result)
            except (ValueError, OSError, ImportError, KeyError, TypeError, json.JSONDecodeError, subprocess.TimeoutExpired) as e:
                self.reply(400, {'error': str(e)})

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=ROOT / 'local/developer-studio')
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args()
    studio = Studio(args.workspace)
    server = ThreadingHTTPServer(('127.0.0.1', args.port), handler_for(studio))
    url = f'http://127.0.0.1:{server.server_port}/'
    print(f'Developer Studio: {url}\nWorkspace: {studio.workspace}\nPress Ctrl+C to stop.', flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()

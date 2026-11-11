"""Run real app_manager.c transactions against disposable files and fake NVS.

Package verification is a test double; this checks storage failure semantics,
not cryptographic verification, flash timing, or physical power-loss recovery.
"""
import os
import shutil
import subprocess
from pathlib import Path
from build_c_app import ROOT, DEFAULT_SDK


def main():
    sdk = Path(os.environ.get('WASI_SDK_PATH', DEFAULT_SDK))
    clang = sdk / 'bin' / ('clang.exe' if os.name == 'nt' else 'clang')
    output = ROOT / 'local/storage-host.wasm'
    output.parent.mkdir(exist_ok=True)
    subprocess.run([
        str(clang), '--target=wasm32-wasip1',
        '--sysroot=' + str(sdk / 'share/wasi-sysroot'),
        '-O1', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'tests/storage_host/include'),
        '-I', str(ROOT / 'firmware/main'),
        str(ROOT / 'tests/storage_host/main.c'), '-o', str(output),
    ], check=True)
    node = shutil.which('node') or str(
        Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
    )
    subprocess.run([node, str(ROOT / 'tools/storage_host_test.mjs'), str(output)], check=True)


if __name__ == '__main__':
    main()

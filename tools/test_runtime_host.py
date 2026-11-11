"""Run real runtime.c host API code against WAMR, NVS and HAL doubles.

Covers argument validation, permission checks and quota accounting. The
interpreter, cryptography and hardware timing are deliberately out of scope:
those have separate host suites and device acceptance scripts.
"""
import os
import shutil
import subprocess
from pathlib import Path
from build_c_app import ROOT, DEFAULT_SDK


def main():
    sdk = Path(os.environ.get('WASI_SDK_PATH', DEFAULT_SDK))
    clang = sdk / 'bin' / ('clang.exe' if os.name == 'nt' else 'clang')
    output = ROOT / 'local/runtime-host.wasm'
    output.parent.mkdir(exist_ok=True)
    subprocess.run([
        str(clang), '--target=wasm32-wasip1',
        '--sysroot=' + str(sdk / 'share/wasi-sysroot'),
        '-O1', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'tests/runtime_host/include'),
        '-I', str(ROOT / 'firmware/main'),
        str(ROOT / 'tests/runtime_host/main.c'), '-o', str(output),
    ], check=True)
    node = shutil.which('node') or str(
        Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
    )
    subprocess.run([node, str(ROOT / 'tools/runtime_host_test.mjs'), str(output)], check=True)


if __name__ == '__main__':
    main()

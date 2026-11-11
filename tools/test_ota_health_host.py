"""Test production OTA gate logic with mock local services and bootloader calls."""
import os
import shutil
import subprocess
from pathlib import Path
from build_c_app import ROOT, DEFAULT_SDK


def main():
    sdk = Path(os.environ.get('WASI_SDK_PATH', DEFAULT_SDK))
    clang = sdk / 'bin' / ('clang.exe' if os.name == 'nt' else 'clang')
    node = shutil.which('node') or str(
        Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'
    )
    for forced in (0, 1):
        output = ROOT / 'local' / f'ota-health-{forced}.wasm'
        output.parent.mkdir(exist_ok=True)
        subprocess.run([
            str(clang), '--target=wasm32-wasip1', '--sysroot=' + str(sdk / 'share/wasi-sysroot'),
            '-O1', '-Wall', '-Wextra', '-Werror', f'-DCONFIG_PLATFORM_OTA_TEST_FAIL_BOOT={forced}',
            '-I', str(ROOT / 'tests/ota_health_host/include'),
            '-I', str(ROOT / 'tests/storage_host/include'),
            '-I', str(ROOT / 'firmware/main'),
            str(ROOT / 'tests/ota_health_host/main.c'), str(ROOT / 'firmware/main/ota_health.c'),
            '-o', str(output),
        ], check=True)
        subprocess.run([node, str(ROOT / 'tools/store_host_test.mjs'), str(output)], check=True)


if __name__ == '__main__':
    main()

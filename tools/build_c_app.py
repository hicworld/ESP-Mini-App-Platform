"""Compile a freestanding C or C++ application against ABI v1, optionally sign it.

The source language is picked from the application directory: main.cpp / main.cc
select clang++, main.c selects clang. Both produce the same reactor ABI.
"""
import argparse
import json
import os
import subprocess
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from app_package import create, prepare_candidate
from node_runtime import node_executable

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SDK = Path('C:/esp/mini-app-sdk/wasi-sdk-34.0-x86_64-windows')
SOURCE_NAMES = ('main.c', 'main.cpp', 'main.cc')
CXX_SUFFIXES = ('.cpp', '.cc')
CXX_FLAGS = ('-std=c++17', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics')


def pick_source(app):
    for name in SOURCE_NAMES:
        candidate = app/name
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        f'No application source in {app}; expected one of {", ".join(SOURCE_NAMES)}')


def build(app, sdk, output, key_path, unsigned=False):
    app, sdk, output = app.resolve(), sdk.resolve(), output.resolve()
    source = pick_source(app)
    is_cxx = source.suffix in CXX_SUFFIXES
    base = 'clang++' if is_cxx else 'clang'
    compiler = sdk/'bin'/(base+'.exe' if os.name == 'nt' else base)
    if not compiler.is_file():
        raise FileNotFoundError(f'WASM compiler missing: {compiler}; run tools/setup_sdk.ps1 or set WASI_SDK_PATH')
    output.mkdir(parents=True, exist_ok=True)
    wasm_path = output/(app.name+'.wasm')
    command = [
        str(compiler), '--target=wasm32-unknown-unknown', '-mcpu=mvp',
        '-nostdlib', '-ffreestanding', '-fno-builtin', '-Oz', '-Wall', '-Wextra', '-Werror',
    ]
    if is_cxx:
        command += list(CXX_FLAGS)
    command += [
        '-I', str(ROOT/'sdk/include'), str(source),
        '-Wl,--no-entry,--export=app_init,--export=app_event,--export-memory',
        '-Wl,--stack-first,-z,stack-size=16384,--initial-memory=65536,--max-memory=262144,--strip-all',
        '-o', str(wasm_path),
    ]
    subprocess.run(command, check=True)
    # Compile-and-link success says nothing about the reactor ABI; check before signing.
    subprocess.run([node_executable(), str(ROOT/'tools/abi_check.mjs'), str(wasm_path)], check=True)
    manifest = json.loads((app/'manifest.json').read_text(encoding='utf-8'))
    wasm = wasm_path.read_bytes()
    prepare_candidate(manifest, wasm)
    if unsigned:
        print(f'Built unsigned {wasm_path}: {len(wasm)} bytes; no .app was created')
        return wasm_path
    key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
    package = create(manifest, wasm, key)
    package_path = output/(app.name+'.app')
    package_path.write_bytes(package)
    print(f'Built {wasm_path.name} and signed {package_path.name}: {len(package)} bytes')
    return package_path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('WASI_SDK_PATH', DEFAULT_SDK)))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--key', type=Path)
    parser.add_argument('--unsigned', action='store_true', help='Build and validate WASM without a signing key or .app output')
    args = parser.parse_args()
    if args.unsigned and args.key:
        parser.error('--unsigned and --key cannot be combined')
    output = args.output or ROOT/'dist'/('submission' if args.unsigned else '')
    build(args.app, args.sdk, output, args.key or ROOT/'local/dev_private.pem', args.unsigned)

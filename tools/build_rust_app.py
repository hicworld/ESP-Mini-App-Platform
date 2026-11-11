"""Compile a freestanding Rust application against ABI v1, then sign its EMAP package.

Uses the wasm32-unknown-unknown target, which is freestanding and therefore
imports no WASI. The linker flags below are not optional decoration: without
them rustc emits a module with a default multi-megabyte memory declaration and
a stack placed after the data, both of which the device refuses.

Unlike the C/C++ chain this one needs a Rust toolchain that is not part of the
project bootstrap. When it is missing this script fails with instructions
instead of silently producing nothing.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from app_package import create, prepare_candidate
from node_runtime import node_executable

ROOT = Path(__file__).resolve().parents[1]
TARGET = 'wasm32-unknown-unknown'

LINK_ARGS = (
    '--no-entry',
    '--export=app_init',
    '--export=app_event',
    '--export-memory',
    '--stack-first', '-z', 'stack-size=16384',
    '--initial-memory=65536',
    '--max-memory=262144',
    '--strip-all',
)

MISSING_CARGO = """Rust toolchain not found: no `cargo` on PATH.

This build chain needs rustup plus the {target} target:
  1. Install rustup:  https://rustup.rs   (or:  winget install Rustlang.Rustup)
  2. Add the target:  rustup target add {target}
  3. Build again:     python tools/build_rust_app.py apps/rust_bounce

Nothing was built. The C and C++ chains (tools/build_c_app.py) use the bundled
WASI SDK and are unaffected by this."""

MISSING_TARGET = """The {target} target is not installed for the active toolchain.

Run:  rustup target add {target}
Then: python tools/build_rust_app.py apps/rust_bounce"""


def crate_name(app):
    match = re.search(r'^name\s*=\s*"([^"]+)"', (app/'Cargo.toml').read_text(encoding='utf-8'), re.M)
    if not match:
        raise ValueError(f'cannot read [package] name from {app/"Cargo.toml"}')
    return match.group(1).replace('-', '_')


def rustup_bin():
    """Locate rustup's bin directory.

    A rustup install frequently leaves ~/.cargo/bin off PATH, so `cargo` is
    missing even though the toolchain is present. Falling back to the default
    location keeps the error message honest: it should only claim Rust is absent
    when it really is.
    """
    if shutil.which('cargo'):
        return None
    candidate = Path.home()/'.cargo'/'bin'
    if (candidate/('cargo.exe' if os.name == 'nt' else 'cargo')).is_file():
        return candidate
    raise SystemExit(MISSING_CARGO.format(target=TARGET))


def require_toolchain():
    """Return the build environment and the absolute path to cargo.

    An absolute path is required, not just a PATH entry: Windows CreateProcess
    resolves bare names against the calling process's PATH, not the env passed
    to the child, so "cargo" would not be found even with PATH fixed up.
    """
    extra = rustup_bin()
    env = dict(os.environ)
    rustup = 'rustup'
    if extra:
        env['PATH'] = str(extra) + os.pathsep + env.get('PATH', '')
        rustup = str(extra/('rustup.exe' if os.name == 'nt' else 'rustup'))
    cargo = shutil.which('cargo', path=env['PATH'])
    if not cargo:
        raise SystemExit(MISSING_CARGO.format(target=TARGET))
    if shutil.which('rustup', path=env['PATH']):
        listed = subprocess.run([rustup, 'target', 'list', '--installed'],
                                capture_output=True, text=True, env=env)
        if TARGET not in listed.stdout:
            raise SystemExit(MISSING_TARGET.format(target=TARGET))
    return env, cargo


def build(app, output, key_path, target_dir=None, unsigned=False):
    app, output = app.resolve(), output.resolve()
    if not (app/'Cargo.toml').is_file():
        raise FileNotFoundError(f'no Cargo.toml in {app}')
    output.mkdir(parents=True, exist_ok=True)
    env, cargo = require_toolchain()
    env['RUSTFLAGS'] = ' '.join(f'-C link-arg={a}' for a in LINK_ARGS)
    command = [cargo, 'build', '--release', '--target', TARGET,
               '--manifest-path', str(app/'Cargo.toml')]
    if target_dir:
        command += ['--target-dir', str(target_dir)]
    subprocess.run(command, check=True, env=env)

    built = Path(target_dir or app/'target')/TARGET/'release'/(crate_name(app)+'.wasm')
    if not built.is_file():
        raise FileNotFoundError(f'expected wasm artifact not produced: {built}')

    wasm_path = output/(app.name+'.wasm')
    wasm_path.write_bytes(built.read_bytes())

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
    parser.add_argument('--output', type=Path)
    parser.add_argument('--key', type=Path)
    parser.add_argument('--unsigned', action='store_true', help='Build and validate WASM without a signing key or .app output')
    parser.add_argument('--target-dir', type=Path, default=None)
    args = parser.parse_args()
    if args.unsigned and args.key:
        parser.error('--unsigned and --key cannot be combined')
    output = args.output or ROOT/'dist'/('submission' if args.unsigned else '')
    try:
        build(args.app, output, args.key or ROOT/'local/dev_private.pem', args.target_dir, args.unsigned)
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)

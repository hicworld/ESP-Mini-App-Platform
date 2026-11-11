"""Check an unsigned third-party WASM submission without using a signing key."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from app_package import prepare_candidate
from node_runtime import node_executable

ROOT = Path(__file__).resolve().parents[1]


def no_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'duplicate manifest field: {key}')
        result[key] = value
    return result


def check(app, wasm_path):
    app, wasm_path = app.resolve(), wasm_path.resolve()
    manifest_path = app/'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'),
                          object_pairs_hook=no_duplicate_keys)
    wasm = wasm_path.read_bytes()
    prepare_candidate(manifest, wasm)
    subprocess.run([node_executable(), str(ROOT/'tools/abi_check.mjs'), str(wasm_path)], check=True)
    digest = hashlib.sha256(wasm).hexdigest()
    print(f'Submission OK: {manifest["id"]} {manifest["version"]}')
    print(f'WASM: {wasm_path} ({len(wasm)} bytes)')
    print(f'SHA256: {digest}')
    print('Unsigned only: this file cannot be installed until the platform owner reviews and signs it.')
    return digest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path, help='App source directory containing manifest.json')
    parser.add_argument('wasm', type=Path, nargs='?', help='Unsigned WASM path')
    args = parser.parse_args()
    check(args.app, args.wasm or ROOT/'dist/submission'/(args.app.name+'.wasm'))

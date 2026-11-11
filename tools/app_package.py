"""EMAP v1: fixed header, signed manifest, single WASM payload. No archive extraction."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

HEADER = struct.Struct('<4sIII')
MAX_WASM = 256 * 1024
MAX_MANIFEST = 2048
ID_RE = re.compile(r'[a-z][a-z0-9]*(?:\.[a-z][a-z0-9]*){2,}')
SEMVER_RE = re.compile(r'[0-9]+\.[0-9]+\.[0-9]+')
HARDWARE = ('display', 'touch', 'speaker', 'motion', 'buttons', 'wifi',
            'camera', 'microphone', 'temperature', 'humidity', 'light', 'imu',
            'magnetometer', 'battery', 'haptic', 'led')

def valid_version(value):
    return isinstance(value, str) and len(value) <= 19 and bool(SEMVER_RE.fullmatch(value)) and all(int(p) <= 65535 for p in value.split('.'))

def validate_manifest(m):
    if not isinstance(m, dict):
        raise ValueError('manifest must be an object')
    if any(type(m.get(field)) is not int or m[field] != 1 for field in ('manifest_version','api_version')):
        raise ValueError('unsupported manifest/API version')
    if not isinstance(m.get('id'), str) or not ID_RE.fullmatch(m['id']) or len(m['id']) >= 48:
        raise ValueError('invalid application id')
    for field, limit in [('name', 39), ('version', 19)]:
        if not isinstance(m.get(field), str) or not 1 <= len(m[field].encode()) <= limit or '\0' in m[field]:
            raise ValueError(f'invalid {field}')
    if not valid_version(m['version']):
        raise ValueError('version must be X.Y.Z')
    if not valid_version(m.get('platform_min', '0.1.0')):
        raise ValueError('platform_min must be X.Y.Z')
    for field, limit in [('author', 80), ('description', 500)]:
        if field in m:
            value = m[field]
            allowed_controls = '\n\t' if field == 'description' else ''
            if (not isinstance(value, str) or not 1 <= len(value.encode('utf-8')) <= limit
                    or any(ord(char) < 32 and char not in allowed_controls for char in value)):
                raise ValueError(f'invalid {field}')
    if m.get('entry') != 'main.wasm':
        raise ValueError('only main.wasm is supported')
    for field in ['permissions', 'hardware', 'optional_hardware']:
        values = m.get(field)
        allowed = ('display', 'touch', 'storage', 'environment', 'input', 'audio', 'sensors', 'feedback') if field == 'permissions' else HARDWARE
        if field == 'optional_hardware' and values is None:
            values = []
        if not isinstance(values, list) or any(v not in allowed for v in values):
            raise ValueError(f'unsupported {field}')
        if len(set(values)) != len(values):
            raise ValueError(f'duplicate {field}')
    if set(m['hardware']) & set(m.get('optional_hardware', [])):
        raise ValueError('hardware cannot be both required and optional')

def prepare_candidate(manifest, wasm):
    """Validate an unsigned submission with the same limits used for signing."""
    m = dict(manifest)
    m.setdefault('platform_min', '0.1.0')
    m.setdefault('optional_hardware', [])
    validate_manifest(m)
    if not wasm.startswith(b'\0asm\x01\0\0\0') or len(wasm) > MAX_WASM:
        raise ValueError('invalid/oversized WASM')
    m['wasm_size'] = len(wasm)
    m['sha256'] = hashlib.sha256(wasm).hexdigest()
    encoded = json.dumps(m, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode()
    if len(encoded) > MAX_MANIFEST:
        raise ValueError('manifest too large')
    return encoded

def create(manifest, wasm, private_key):
    encoded = prepare_candidate(manifest, wasm)
    signature = private_key.sign(encoded, ec.ECDSA(hashes.SHA256()))
    return HEADER.pack(b'EMAP', len(encoded), len(wasm), len(signature)) + encoded + signature + wasm

def verify(package, public_key):
    if len(package) < HEADER.size:
        raise ValueError('truncated header')
    magic, ml, wl, sl = HEADER.unpack_from(package)
    if magic != b'EMAP' or not 1 <= ml <= MAX_MANIFEST or not 8 <= wl <= MAX_WASM or not 8 <= sl <= 80:
        raise ValueError('invalid package header')
    if len(package) != 16 + ml + sl + wl:
        raise ValueError('package size mismatch')
    raw = package[16:16+ml]
    public_key.verify(package[16+ml:16+ml+sl], raw, ec.ECDSA(hashes.SHA256()))
    def unique(pairs):
        out = {}
        for k, v in pairs:
            if k in out:
                raise ValueError('duplicate JSON key')
            out[k] = v
        return out
    m = json.loads(raw, object_pairs_hook=unique)
    validate_manifest(m)
    wasm = package[16+ml+sl:]
    if m.get('wasm_size') != wl or m.get('sha256') != hashlib.sha256(wasm).hexdigest():
        raise ValueError('payload hash/size mismatch')
    if not wasm.startswith(b'\0asm\x01\0\0\0'):
        raise ValueError('invalid WASM magic')
    return m, wasm

def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    k = sub.add_parser('keygen'); k.add_argument('--private', type=Path, required=True); k.add_argument('--public', type=Path, required=True)
    b = sub.add_parser('pack'); b.add_argument('manifest', type=Path); b.add_argument('wasm', type=Path); b.add_argument('output', type=Path); b.add_argument('--key', type=Path, required=True)
    v = sub.add_parser('verify'); v.add_argument('package', type=Path); v.add_argument('--key', type=Path, required=True)
    args = p.parse_args()
    if args.command == 'keygen':
        if args.private.exists() or args.public.exists():
            p.error('key output already exists; refusing to replace trust identity')
        key = ec.generate_private_key(ec.SECP256R1())
        args.private.parent.mkdir(parents=True, exist_ok=True)
        args.public.parent.mkdir(parents=True, exist_ok=True)
        args.private.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        args.public.write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    elif args.command == 'pack':
        key = serialization.load_pem_private_key(args.key.read_bytes(), password=None)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(create(json.loads(args.manifest.read_text(encoding='utf-8')), args.wasm.read_bytes(), key))
        print(f'Packaged {args.output.name}')
    else:
        key = serialization.load_pem_public_key(args.key.read_bytes())
        m, _ = verify(args.package.read_bytes(), key)
        print(json.dumps(m, indent=2))

if __name__ == '__main__':
    main()

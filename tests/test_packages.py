import copy
import importlib.util
from pathlib import Path
import pytest
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('app_package', ROOT/'tools/app_package.py')
pkg = importlib.util.module_from_spec(spec); spec.loader.exec_module(pkg)

@pytest.fixture
def fixture():
    import json
    return (json.loads((ROOT/'apps/hello/manifest.json').read_text()),
            (ROOT/'dist/hello.wasm').read_bytes(), ec.generate_private_key(ec.SECP256R1()))

def test_round_trip(fixture):
    m, wasm, key = fixture
    manifest, restored = pkg.verify(pkg.create(m,wasm,key), key.public_key())
    assert restored == wasm and manifest['id'] == m['id']

@pytest.mark.parametrize('field',['manifest_version','api_version'])
def test_boolean_is_not_an_integer_version(fixture,field):
    m,wasm,key=fixture
    m[field]=True
    with pytest.raises(ValueError,match='manifest/API version'):
        pkg.create(m,wasm,key)

@pytest.mark.parametrize('change', ['signature','payload','truncated','trailing','header'])
def test_reject_corruption(fixture, change):
    m,wasm,key = fixture; data=bytearray(pkg.create(m,wasm,key))
    if change=='signature': data[16+pkg.HEADER.unpack_from(data)[1]+5] ^= 1
    elif change=='payload': data[-1] ^= 1
    elif change=='truncated': data=data[:-1]
    elif change=='trailing': data.extend(b'x')
    else: data[4:8]=b'\xff'*4
    with pytest.raises((ValueError,InvalidSignature)): pkg.verify(bytes(data),key.public_key())

def test_wrong_signer(fixture):
    m,w,key=fixture
    with pytest.raises(InvalidSignature): pkg.verify(pkg.create(m,w,key),ec.generate_private_key(ec.SECP256R1()).public_key())

@pytest.mark.parametrize('field,value', [('id','../../escape'),('api_version',2),('platform_min','bad'),('permissions',['gpio']),('hardware',['gpio']),('optional_hardware',['gpio']),('entry','other.wasm'),('permissions',['display','display']),('name','x'*40),('author','x'*81),('author','bad\nname'),('description','x'*501)])
def test_reject_incompatible_manifest(fixture,field,value):
    m,w,key=fixture; m=copy.deepcopy(m); m[field]=value
    with pytest.raises(ValueError): pkg.create(m,w,key)

def test_hardware_requirements_and_defaults(fixture):
    m,w,key=fixture;m=copy.deepcopy(m)
    m['hardware']=['display','camera'];m['optional_hardware']=['speaker','motion']
    manifest,_=pkg.verify(pkg.create(m,w,key),key.public_key())
    assert manifest['hardware']==['display','camera']
    assert manifest['optional_hardware']==['speaker','motion']
    legacy=copy.deepcopy(m);legacy.pop('optional_hardware')
    assert pkg.verify(pkg.create(legacy,w,key),key.public_key())[0]['optional_hardware']==[]

def test_hardware_cannot_be_required_and_optional(fixture):
    m,w,key=fixture;m=copy.deepcopy(m)
    m['optional_hardware']=['display']
    with pytest.raises(ValueError,match='both required and optional'):
        pkg.create(m,w,key)

def test_oversized_wasm(fixture):
    m,w,key=fixture
    with pytest.raises(ValueError): pkg.create(m,w+b'\0'*pkg.MAX_WASM,key)

def test_legacy_signed_package_without_minimum(fixture):
    import json, hashlib
    from cryptography.hazmat.primitives import hashes
    m,w,key=fixture
    m=dict(m); m.pop('platform_min',None)
    m.update(wasm_size=len(w),sha256=hashlib.sha256(w).hexdigest())
    raw=json.dumps(m).encode()
    signature=key.sign(raw,ec.ECDSA(hashes.SHA256()))
    package=pkg.HEADER.pack(b'EMAP',len(raw),len(w),len(signature))+raw+signature+w
    assert pkg.verify(package,key.public_key())[1]==w

@pytest.mark.parametrize('value', ['0.1.65536','-1.2.3','１.２.３',None,'0.1'])
def test_invalid_version_ranges(fixture,value):
    m,w,key=fixture; m=dict(m); m['platform_min']=value
    with pytest.raises(ValueError): pkg.create(m,w,key)

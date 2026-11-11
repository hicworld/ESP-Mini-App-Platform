import json
import sys
from pathlib import Path
import pytest
from cryptography.hazmat.primitives.asymmetric import ec

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from app_package import create
from build_store import publish


def package(tmp_path, key, name='hello', **details):
    manifest=json.loads((ROOT/'apps/hello/manifest.json').read_text())
    manifest['name']=name
    manifest.update(details)
    path=tmp_path/'test.app'
    path.write_bytes(create(manifest,(ROOT/'dist/hello.wasm').read_bytes(),key))
    return path


def test_static_store_integrity_and_html_escape(tmp_path):
    import hashlib
    key=ec.generate_private_key(ec.SECP256R1())
    source=package(tmp_path,key,'<script>',author='<img src=x onerror=1>',description='<script>about</script>')
    out=tmp_path/'store'
    index=publish([source],key.public_key(),out)
    entry=index['apps'][0]
    assert (out/entry['file']).read_bytes()==source.read_bytes()
    assert entry['sha256']==hashlib.sha256(source.read_bytes()).hexdigest()
    assert entry['size']==len(source.read_bytes())
    assert json.loads((out/'index.json').read_text())==index
    assert '&lt;script&gt;' in (out/'index.html').read_text(encoding='utf-8')
    assert '<script>' not in (out/'index.html').read_text(encoding='utf-8')
    detail=(out/'details'/f'{entry["sha256"]}.html').read_text(encoding='utf-8')
    assert '作者：&lt;img src=x onerror=1&gt;' in detail
    assert '&lt;script&gt;about&lt;/script&gt;' in detail
    assert '<img' not in detail and '<script>' not in detail
    assert f'../{entry["file"]}' in detail
    assert not list(out.rglob('*.pem'))


def test_bad_package_preserves_published_index(tmp_path):
    key=ec.generate_private_key(ec.SECP256R1())
    source=package(tmp_path,key)
    out=tmp_path/'store'; index=publish([source],key.public_key(),out)
    assert '作者：未填写' in (out/'details'/f'{index["apps"][0]["sha256"]}.html').read_text(encoding='utf-8')
    before=(out/'index.json').read_bytes()
    corrupt=bytearray(source.read_bytes()); corrupt[-1]^=1; source.write_bytes(corrupt)
    with pytest.raises(ValueError): publish([source],key.public_key(),out)
    assert (out/'index.json').read_bytes()==before


def test_duplicate_ids_rejected_before_publish(tmp_path):
    key=ec.generate_private_key(ec.SECP256R1()); source=package(tmp_path,key)
    out=tmp_path/'store'
    with pytest.raises(ValueError,match='one version'): publish([source,source],key.public_key(),out)
    assert not out.exists()

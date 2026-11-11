import hashlib
import io
import json
import sys
import threading
import urllib.error
import urllib.request
import zipfile
from http.server import ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from developer_studio import Studio, handler_for
from review_submission import audit_bytes, check_capabilities


def section(kind, payload):
    assert len(payload) < 128
    return bytes((kind, len(payload))) + payload


def name(value):
    encoded = value.encode()
    return bytes((len(encoded),)) + encoded


def minimal_wasm():
    # Two empty functions, one memory, the required reactor exports.
    types = section(1, b'\x02\x60\x00\x00\x60\x03\x7f\x7f\x7f\x00')
    functions = section(3, b'\x02\x00\x01')
    memory = section(5, b'\x01\x01\x01\x04')
    exports = (b'\x03' + name('memory') + b'\x02\x00' +
               name('app_init') + b'\x00\x00' +
               name('app_event') + b'\x00\x01')
    code = section(10, b'\x02\x02\x00\x0b\x02\x00\x0b')
    return b'\0asm\x01\0\0\0' + types + functions + memory + section(7, exports) + code


def submission(*, bad_checksum=False, extra=None):
    wasm = minimal_wasm()
    manifest = {'manifest_version': 1, 'api_version': 1, 'id': 'org.example.demo',
                'name': 'Demo', 'version': '0.1.0', 'platform_min': '0.5.0',
                'entry': 'main.wasm', 'description': 'Demo app', 'author': 'Tester',
                'permissions': ['display'], 'hardware': ['display'], 'optional_hardware': []}
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('demo/manifest.json', json.dumps(manifest))
        archive.writestr('demo/main.c', 'void app_init(void) {}\n')
        archive.writestr('demo/demo.wasm', wasm)
        archive.writestr('demo/投稿信息.md', '作者 Tester；原创代码；无第三方素材。')
        digest = '0' * 64 if bad_checksum else hashlib.sha256(wasm).hexdigest()
        archive.writestr('SHA256SUMS.txt', f'{digest}  demo/demo.wasm\n')
        if extra:
            archive.writestr(*extra)
    return stream.getvalue()


def test_valid_zip_enters_manual_review():
    report = audit_bytes(submission(), 'demo.zip')
    assert report['status'] == 'MANUAL_REVIEW'
    assert report['counts']['error'] == 0
    assert {'abi', 'checksum', 'capabilities'} <= {f['code'] for f in report['findings']}


def test_rejects_checksum_mismatch_and_unsafe_path():
    checksum = audit_bytes(submission(bad_checksum=True))
    assert checksum['status'] == 'REJECTED'
    assert 'checksum' in {f['code'] for f in checksum['findings']}
    unsafe = audit_bytes(submission(extra=('../outside.txt', 'bad')))
    assert unsafe['status'] == 'REJECTED'
    assert 'unsafe_entry' in {f['code'] for f in unsafe['findings']}


def test_rejects_missing_permission_and_hardware():
    manifest = {'permissions': [], 'hardware': [], 'optional_hardware': [], 'platform_min': '0.4.0'}
    report = {'findings': []}
    check_capabilities(manifest, [{'module': 'platform', 'name': 'imu', 'kind': 'function'}], report)
    assert {'missing_permission', 'missing_hardware', 'platform_min'} == {f['code'] for f in report['findings']}


def test_review_endpoint_requires_token_and_returns_report(tmp_path):
    studio = Studio(tmp_path)
    server = ThreadingHTTPServer(('127.0.0.1', 0), handler_for(studio))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f'http://127.0.0.1:{server.server_port}'
    try:
        with urllib.request.urlopen(base + '/api/session') as response:
            token = json.load(response)['token']
        request = urllib.request.Request(base + '/api/review-upload', submission(), method='POST',
                                         headers={'X-Studio-Token': token,
                                                  'Content-Type': 'application/zip'})
        with urllib.request.urlopen(request) as response:
            assert json.load(response)['status'] == 'MANUAL_REVIEW'
        denied = urllib.request.Request(base + '/api/review-upload', submission(), method='POST',
                                        headers={'Content-Type': 'application/zip'})
        try:
            urllib.request.urlopen(denied)
            assert False, 'unauthorized upload accepted'
        except urllib.error.HTTPError as exc:
            assert exc.code == 403
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)

import hashlib
import json
import sys
import threading
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from developer_studio import Studio, handler_for


def test_browser_editor_saves_source_and_invalidates_build(tmp_path):
    studio = Studio(tmp_path)
    studio.create('demo')
    files = studio.edit_files('demo')
    assert 'main.c' in files and 'manifest.json' not in files
    before = studio.read_file('demo', 'main.c')
    output = studio.builds / 'demo' / 'demo.wasm'
    output.parent.mkdir(parents=True)
    output.write_bytes(b'old build')
    changed = before['content'] + '\n/* browser edit */\n'
    result = studio.save_file('demo', 'main.c', changed, before['sha256'])
    assert result['sha256'] == hashlib.sha256(changed.encode()).hexdigest()
    assert studio.read_file('demo', 'main.c')['content'] == changed
    assert not output.exists()


def test_browser_editor_rejects_conflict_and_non_source_paths(tmp_path):
    studio = Studio(tmp_path)
    studio.create('demo')
    source = studio.read_file('demo', 'main.c')
    (studio.projects / 'demo' / 'main.c').write_text('external edit', encoding='utf-8')
    with pytest.raises(ValueError, match='其他编辑器修改'):
        studio.save_file('demo', 'main.c', 'browser edit', source['sha256'])
    assert (studio.projects / 'demo' / 'main.c').read_text() == 'external edit'
    for path in ('../demo/main.c', 'manifest.json', 'main.c/../manifest.json', 'main.c\\x'):
        with pytest.raises(ValueError):
            studio.read_file('demo', path)
    assert json.loads((studio.projects / 'demo' / 'manifest.json').read_text())


def test_remove_and_restore_workbench_copy(tmp_path):
    studio = Studio(tmp_path)
    studio.create('demo')
    original = (studio.projects / 'demo' / 'main.c').read_bytes()
    output = studio.builds / 'demo' / 'demo.wasm'
    output.parent.mkdir(parents=True)
    output.write_bytes(b'built')
    removed = studio.remove_project('demo')
    assert studio.list() == []
    assert studio.removed_list()[0]['id'] == removed['id']
    assert not output.exists()
    assert (studio.removed / removed['id'] / 'project' / 'main.c').read_bytes() == original
    with pytest.raises(ValueError):
        studio.restore_project('../demo')
    restored = studio.restore_project(removed['id'])
    assert restored['project'] == 'demo'
    assert output.read_bytes() == b'built'
    assert studio.removed_list() == []


def test_restore_keeps_archive_when_name_is_taken(tmp_path):
    studio = Studio(tmp_path)
    studio.create('demo')
    removed = studio.remove_project('demo')
    studio.create('demo')
    with pytest.raises(ValueError, match='同名项目'):
        studio.restore_project(removed['id'])
    assert studio.removed_list()[0]['id'] == removed['id']


def test_document_viewer_only_serves_known_guides():
    docs = Studio.docs()
    assert {'api', 'guide', 'examples'} <= {item['id'] for item in docs}
    assert 'platform_clear' in Studio.document('api')['markdown']
    with pytest.raises(ValueError, match='unknown document'):
        Studio.document('../local/dev_private.pem')


def test_examples_show_real_code_and_copy_without_changing_original(tmp_path):
    studio = Studio(tmp_path)
    catalog = studio.examples()
    assert {'c_color_pad', 'c_scene_timer', 'c_optional_input', 'rust_saved_counter', 'cpp_bounce'} <= {item['id'] for item in catalog}
    detail = studio.example('c_color_pad')
    source = ROOT / 'examples/c_color_pad/main.c'
    assert detail['files'][0]['content'] == source.read_text(encoding='utf-8')
    first = studio.copy_example('c_color_pad')
    second = studio.copy_example('c_color_pad')
    assert first['project'] == 'c_color_pad'
    assert second['project'] == 'c_color_pad_2'
    assert (studio.projects / first['project'] / 'main.c').read_text(encoding='utf-8') == source.read_text(encoding='utf-8')
    rust = studio.copy_example('rust_saved_counter')
    rust_dir = studio.projects / rust['project']
    assert 'vendor/miniapp' in (rust_dir / 'Cargo.toml').read_text(encoding='utf-8')
    assert (rust_dir / 'vendor/miniapp/src/lib.rs').is_file()
    with pytest.raises(ValueError, match='unknown example'):
        studio.example('../../local/dev_private.pem')


def test_browser_remove_restore_and_docs_endpoints(tmp_path):
    studio = Studio(tmp_path)
    studio.create('demo')
    server = ThreadingHTTPServer(('127.0.0.1', 0), handler_for(studio))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f'http://127.0.0.1:{server.server_port}'

    def call(path, payload=None, token=''):
        body = None if payload is None else json.dumps(payload).encode()
        headers = {'X-Studio-Token': token}
        if body is not None:
            headers['Content-Type'] = 'application/json'
        request = urllib.request.Request(base + path, body, headers)
        with urllib.request.urlopen(request) as response:
            return json.load(response)

    try:
        token = call('/api/session')['token']
        assert 'remove_restore_v1' in call('/api/session')['capabilities']
        for path in ('/', '/project?name=demo', '/preview?name=demo', '/app?name=demo', '/examples', '/docs'):
            with urllib.request.urlopen(base + path) as response:
                assert response.headers['Content-Type'].startswith('text/html')
                html = response.read().decode('utf-8')
                assert '<nav' in html
                if path == '/':
                    assert 'id="screen"' not in html
                    assert 'id="editor"' not in html
        with urllib.request.urlopen(base + '/studio.css') as response:
            assert response.headers['Content-Type'].startswith('text/css')
        assert 'platform_text' in call('/api/doc?id=api', token=token)['markdown']
        assert 'c_scene_timer' in {item['id'] for item in call('/api/examples', token=token)}
        assert 'platform_clear' in call('/api/example?id=c_color_pad', token=token)['files'][0]['content']
        copied = call('/api/example-copy', {'id':'c_scene_timer'}, token)
        assert copied['project'] == 'c_scene_timer'
        removed = call('/api/remove', {'name': 'demo'}, token)
        assert len(call('/api/projects')) == 1
        assert call('/api/removed', token=token)[0]['id'] == removed['id']
        assert call('/api/restore', {'id': removed['id']}, token)['project'] == 'demo'
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)

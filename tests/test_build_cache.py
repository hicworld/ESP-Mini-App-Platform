import importlib.util
from pathlib import Path
import pytest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('prepare_build',ROOT/'tools/prepare_build.py')
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)

def source(tmp_path):
    root=tmp_path/'source'
    for directory in ['firmware/main','firmware/components','external/wamr/core','external/wamr/build-scripts']:
        (root/directory).mkdir(parents=True)
    for name in ['CMakeLists.txt','partitions.csv','sdkconfig.defaults']:
        (root/'firmware'/name).write_text('fixture')
    return root

def test_sync_changes_and_removes_only_owned_files(tmp_path):
    src=source(tmp_path); cache=tmp_path/'cache'
    file=src/'firmware/main/app.c'; file.write_text('one')
    snapshot=module.prepare(src,cache)
    unowned=snapshot/'notes.txt'; unowned.write_text('retain')
    file.write_text('two'); module.prepare(src,cache)
    assert (snapshot/'firmware/main/app.c').read_text()=='two'
    file.unlink(); module.prepare(src,cache)
    assert not (snapshot/'firmware/main/app.c').exists()
    assert unowned.read_text()=='retain'

def test_nonempty_unowned_cache_is_rejected(tmp_path):
    src=source(tmp_path); cache=tmp_path/'cache'; cache.mkdir()
    (cache/'personal.txt').write_text('retain')
    with pytest.raises(ValueError): module.prepare(src,cache)
    assert (cache/'personal.txt').read_text()=='retain'

def test_snapshot_cannot_remove_outside_file(tmp_path):
    src=source(tmp_path); cache=tmp_path/'cache'; module.prepare(src,cache)
    outside=cache/'outside.txt'; outside.write_text('retain')
    (cache/'synced.json').write_text('["../outside.txt"]')
    with pytest.raises(ValueError): module.prepare(src,cache)
    assert outside.read_text()=='retain'

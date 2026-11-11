"""Run product Wi-Fi UI with real LVGL under WASI, including rendered screenshots."""
import os, subprocess, shutil, hashlib
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from build_c_app import ROOT, DEFAULT_SDK
sdk=Path(os.environ.get('WASI_SDK_PATH',DEFAULT_SDK))
# managed_components/ is ignored by Git, so a fresh clone has no local copy.
# Fall back to the build cache the same way test_store_host.py does for cJSON.
lv=ROOT/'firmware/managed_components/lvgl__lvgl'
if not (lv/'src').is_dir():
    lv=Path('C:/esp/mini-app-platform-build/src/firmware/managed_components/lvgl__lvgl')
if not (lv/'src').is_dir():
    raise SystemExit(f'LVGL sources not found: {lv}; run ./Run.ps1 Build once to populate the build cache')
out=ROOT/'local/network-ui-host';out.mkdir(parents=True,exist_ok=True)
fixtures=ROOT/'tests/network_ui_host'
shutil.copyfile(ROOT/'firmware/main/network_setup.c',out/'network_setup.c')
config=b''.join(p.read_bytes() for p in sorted(fixtures.rglob('*.h')))
base=[str(sdk/'bin/clang.exe'),'--target=wasm32-wasip1','--sysroot='+str(sdk/'share/wasi-sysroot'),'-O1','-DLV_CONF_INCLUDE_SIMPLE','-I'+str(fixtures),'-I'+str(out),'-I'+str(lv)]
sources=list((lv/'src').rglob('*.c'))+[fixtures/'main.c',ROOT/'firmware/main/garden_font.c']
def compile_one(src):
    obj=out/(hashlib.sha256(str(src).encode()).hexdigest()[:20]+'.o')
    stamp=obj.with_suffix('.sha');digest=hashlib.sha256(src.read_bytes()+config+(out/'network_setup.c').read_bytes() if src.name=='main.c' else src.read_bytes()+config).hexdigest()
    if not obj.exists() or not stamp.exists() or stamp.read_text()!=digest:
        subprocess.run(base+['-c',str(src),'-o',str(obj)],check=True,capture_output=True);stamp.write_text(digest)
    return obj
try:
    with ThreadPoolExecutor(max_workers=8) as pool: objects=list(pool.map(compile_one,sources))
except subprocess.CalledProcessError as e:
    print(e.stderr.decode(errors='replace'));raise
rsp=out/'link.rsp';rsp.write_text('\n'.join(p.name for p in objects),encoding='ascii')
subprocess.run(base+['@link.rsp','-Wl,-z,stack-size=1048576','-lm','-o','test.wasm'],check=True,cwd=out)
subprocess.run(['node',str(ROOT/'tools/network_ui_runner.mjs'),str(out/'test.wasm'),str(out)],check=True)
from PIL import Image
for p in out.glob('*.ppm'):Image.open(p).save(p.with_suffix('.png'))

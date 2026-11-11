"""Compile the actual firmware directory parser and cJSON for a host-only WASI test."""
import os
import shutil
import subprocess
from pathlib import Path
from build_c_app import ROOT, DEFAULT_SDK

sdk=Path(os.environ.get('WASI_SDK_PATH',DEFAULT_SDK))
clang=sdk/'bin'/('clang.exe' if os.name=='nt' else 'clang')
cjson=ROOT/'firmware/managed_components/espressif__cjson/cJSON'
if not (cjson/'cJSON.c').is_file():
    cjson=Path('C:/esp/mini-app-platform-build/src/firmware/managed_components/espressif__cjson/cJSON')
output=ROOT/'local/store-host.wasm'; output.parent.mkdir(exist_ok=True)
subprocess.run([str(clang),'--target=wasm32-wasip1','--sysroot='+str(sdk/'share/wasi-sysroot'),'-O2','-ffunction-sections','-fdata-sections',
    '-I',str(ROOT/'tests/store_host/include'),'-I',str(ROOT/'firmware/main'),'-I',str(cjson),
    str(ROOT/'tests/store_host/main.c'),str(ROOT/'firmware/main/store.c'),str(cjson/'cJSON.c'),
    '-Wl,--gc-sections','-o',str(output)],check=True)
node=shutil.which('node')
if node is None:
    node=str(Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe')
subprocess.run([node,str(ROOT/'tools/store_host_test.mjs'),str(output)],check=True)

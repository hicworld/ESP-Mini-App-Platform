import os
import subprocess
from pathlib import Path
from build_c_app import ROOT, DEFAULT_SDK
sdk=Path(os.environ.get('WASI_SDK_PATH',DEFAULT_SDK))
out=ROOT/'local/scene-host.wasm'
subprocess.run([str(sdk/'bin/clang.exe'),'--target=wasm32-wasip1','--sysroot='+str(sdk/'share/wasi-sysroot'),'-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'firmware/main'),str(ROOT/'tests/scene_host/main.c'),str(ROOT/'firmware/main/scene_render.c'),'-lm','-o',str(out)],check=True)
subprocess.run(['node',str(ROOT/'tools/store_host_test.mjs'),str(out)],check=True)

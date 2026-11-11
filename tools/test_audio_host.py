"""Render original scores using the exact device DSP and check bounded output."""
import subprocess,shutil
from build_c_app import ROOT,DEFAULT_SDK
out=ROOT/'dist/audio';out.mkdir(exist_ok=True,parents=True)
target=ROOT/'local/audio-host.wasm'
subprocess.run([str(DEFAULT_SDK/'bin/clang.exe'),'--target=wasm32-wasip1','--sysroot='+str(DEFAULT_SDK/'share/wasi-sysroot'),'-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'firmware/main'),str(ROOT/'tests/audio_host.c'),str(ROOT/'firmware/main/audio_synth.c'),'-o',str(target)],check=True)
subprocess.run(['node',str(ROOT/'tools/network_ui_runner.mjs'),str(target),str(out)],check=True)
shutil.copyfile(ROOT/'tools/audio_preview.html',out/'index.html')

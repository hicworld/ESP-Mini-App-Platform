"""Generate a standalone browser host for the real compiled garden WASM."""
import base64
from pathlib import Path
from build_c_app import ROOT
template=ROOT/'tools/garden_preview.html'
output=ROOT/'dist/garden-preview.html'
output.write_text(template.read_text(encoding='utf-8').replace('__WASM__',base64.b64encode((ROOT/'dist/garden.wasm').read_bytes()).decode()),encoding='utf-8')
print(output)

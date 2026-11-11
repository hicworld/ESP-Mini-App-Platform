// Runtime host boundary tests need no filesystem: the code under test never
// touches storage directly, so the NVS double stays in memory.
import {WASI} from 'node:wasi';
import fs from 'node:fs';
try {
  const wasi=new WASI({version:'preview1',args:[],env:{},returnOnExit:true});
  const module=await WebAssembly.compile(fs.readFileSync(process.argv[2]));
  const instance=await WebAssembly.instantiate(module,{wasi_snapshot_preview1:wasi.wasiImport});
  process.exitCode=wasi.start(instance);
} catch (error) {
  console.error(error);
  process.exitCode=1;
}

// Only the test harness imports WASI. Mount a disposable directory at /store.
import {WASI} from 'node:wasi';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
const root=fs.mkdtempSync(path.join(os.tmpdir(),'esp-storage-test-'));
const store=path.join(root,'store');
const nand=path.join(root,'nand');
fs.mkdirSync(store);
fs.mkdirSync(path.join(nand,'apps'),{recursive:true});
fs.mkdirSync(path.join(nand,'download'),{recursive:true});
try {
  const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{'/store':store,'/nand':nand},returnOnExit:true});
  const module=await WebAssembly.compile(fs.readFileSync(process.argv[2]));
  const instance=await WebAssembly.instantiate(module,{wasi_snapshot_preview1:wasi.wasiImport});
  process.exitCode=wasi.start(instance);
} finally {
  fs.rmSync(root,{recursive:true,force:true});
}

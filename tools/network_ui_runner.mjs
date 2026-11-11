import {WASI} from 'node:wasi';
import fs from 'node:fs';
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{'.':process.argv[3]},returnOnExit:true});
const module=await WebAssembly.compile(fs.readFileSync(process.argv[2]));
const instance=await WebAssembly.instantiate(module,{wasi_snapshot_preview1:wasi.wasiImport});
process.exitCode=wasi.start(instance);

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
for (const name of ['hello','touch']) {
  const draws=[]; let instance;
  const imports={platform:{clear:c=>draws.push(['clear',c]),rect:(...a)=>draws.push(['rect',...a]),text:(x,y,p,n,c)=>draws.push(['text',new TextDecoder().decode(new Uint8Array(instance.exports.memory.buffer,p,n))]),millis:()=>100}};
  ({instance}=await WebAssembly.instantiate(fs.readFileSync(path.join(root,'dist',`${name}.wasm`)),imports));
  instance.exports.app_init(); assert.equal(draws[0][0],'clear'); assert.equal(draws[1][0],'text');
  instance.exports.app_event(1,100,0);
  const before=draws.length; instance.exports.app_event(2,100,100);
  assert.equal(draws.length,before+(name==='touch'?1:0));
  assert.throws(()=>instance.exports.memory.grow(4),RangeError);
  console.log(`PASS ${name}: ABI imports, initialization, timer, touch, declared memory maximum`);
}
console.log('Host engine is Node WebAssembly; WAMR on S31 still requires device tests.');

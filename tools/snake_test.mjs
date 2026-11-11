import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const module=await WebAssembly.compile(fs.readFileSync(path.join(root,'dist/snake.wasm')));
for (const i of WebAssembly.Module.imports(module)) {
  assert.equal(i.module,'platform');
  assert.ok(['scene','millis','kv_get','kv_set'].includes(i.name),i.name);
}
assert.ok(!WebAssembly.Module.imports(module).some(i=>['clear','rect','text'].includes(i.name)),
  'atomic renderer must not import legacy immediate drawing');
const storage=new Map(); let instance,now=0,frame=[],calls=0;
const bytes=(p,n)=>new Uint8Array(instance.exports.memory.buffer,p,n);
const string=(p,n)=>new TextDecoder().decode(bytes(p,n));
const imports={platform:{
  scene:(p,count,background)=>{
    calls++;assert.ok(count>0&&count<=96,`scene count ${count}`);frame=[];
    const view=new DataView(instance.exports.memory.buffer);
    for(let i=0;i<count;i++){
      const base=p+i*64,kind=view.getUint32(base,true),rgb=view.getUint32(base+4,true);
      const x=view.getInt32(base+8,true),y=view.getInt32(base+12,true);
      const w=view.getInt32(base+16,true),h=view.getInt32(base+20,true);
      assert.ok(x>=0&&y>=0&&w>0&&h>0&&x+w<=480&&y+h<=400,`command ${kind}: ${x},${y},${w},${h}`);
      if(kind===1)frame.push({type:'rect',x,y,w,h,rgb});
      else if(kind===4){
        const raw=bytes(base+32,32),end=raw.indexOf(0);assert.ok(end>=0,'scene text must be NUL terminated');
        frame.push({type:'text',x,y,text:string(base+32,end),rgb});
      }else assert.fail(`unexpected scene command ${kind}`);
    }
    return background<=0xffffff?0:-1;
  },
  millis:()=>now,
  kv_get:(p,n,out,capacity)=>{const data=storage.get(string(p,n));if (!data)return 0;if(data.length>capacity)return -2;bytes(out,data.length).set(data);return data.length;},
  kv_set:(p,n,value,length)=>{storage.set(string(p,n),Uint8Array.from(bytes(value,length)));return length;},
}};
instance=await WebAssembly.instantiate(module,imports);
function call(fn,...args) {calls=0;instance.exports[fn](...args);assert.ok(calls<=1,`expected at most one atomic frame, got ${calls}`);return calls;}
function touch(x,y){return call('app_event',2,x,y);}
function tick(){now=(now+220)>>>0;return call('app_event',1,now,0);}
const head=()=>frame.find(d=>d.type==='rect'&&d.rgb===0x63dfbd);
const valueAt=x=>frame.find(d=>d.type==='text'&&d.x===x&&d.y===10)?.text;
assert.equal(call('app_init'),1,'initial frame is one atomic scene call'); assert.equal(head().x,181); assert.equal(valueAt(210),'0');
touch(380,350); assert.equal(tick(),1,'moving frame is one atomic scene call'); assert.equal(head().x,201); assert.equal(valueAt(90),'1');
// Immediate reversal is ignored, including a second touch before the next tick.
touch(80,350); tick(); assert.equal(head().x,221);
touch(240,320); touch(80,350); tick(); assert.equal(head().x,221); assert.equal(head().y,131);
for(let i=0;i<6;i++)tick();
assert.ok(frame.some(d=>d.text==='Tap: restart'));
assert.equal(new DataView(storage.get('best').buffer).getUint32(0,true),1);
touch(100,100); assert.equal(valueAt(90),'0'); assert.equal(valueAt(210),'1');
// A fresh WASM instance reads the saved high score.
instance=await WebAssembly.instantiate(module,imports); call('app_init');assert.equal(valueAt(210),'1');
// Unsigned millisecond wraparound must keep the timer moving.
now=0xffffff80; call('app_init');touch(380,350);const x=head().x;tick();assert.equal(head().x,x+20);
assert.throws(()=>instance.exports.memory.grow(4),RangeError);
console.log('PASS Snake C/WASM: ABI, movement, eating, reversal, collision, restart, persisted high score, clock wrap and drawing bounds');

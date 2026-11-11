import fs from 'node:fs';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('../',import.meta.url));
const wasm=await WebAssembly.compile(fs.readFileSync(root+'dist/garden.wasm'));
let instance,now=0,frame,background,frames=0,maxCommands=0,settingsRequests=0;
let environment={flags:0,seconds:0,temperature:0,weather:0,daylight:-1,weather_age:0,location:''};
const decoder=new TextDecoder();
const sounds=[];let volume=55;
const platform={
 audio:(k,v)=>{sounds.push([k,v]);if(k===3){if(v>=0)volume=v;return volume;}return 0;},
 millis:()=>now,
 settings:()=>{settingsRequests++;},
 environment:(ptr,capacity)=>{
  assert.equal(capacity,56);const v=new DataView(instance.exports.memory.buffer);const data=new Uint8Array(v.buffer,ptr,56);data.fill(0);
  ['flags','seconds','temperature','weather','daylight','weather_age'].forEach((k,i)=>v.setInt32(ptr+4*i,environment[k],true));
  data.set(new TextEncoder().encode(environment.location).slice(0,31),24);return 56;
 },
 scene:(ptr,count,bg)=>{
  assert.ok(count>0&&count<=96);maxCommands=Math.max(maxCommands,count);let area=0;frame=[];background=bg;
  const v=new DataView(instance.exports.memory.buffer);
  for(let i=0;i<count;i++){
   const p=ptr+i*64,words=Array.from({length:8},(_,j)=>v.getInt32(p+j*4,true));
   const [kind,rgb,x,y,w,h,r,q]=words;
   assert.ok(kind>=1&&kind<=4);assert.ok(x>=0&&y>=0&&x<480&&y<400);
   if(kind===3){assert.ok(w>=0&&w<480&&h>=0&&h<400&&r>=0&&r<480&&q>=0&&q<400);area+=(Math.max(x,w,r)-Math.min(x,w,r)+1)*(Math.max(y,h,q)-Math.min(y,h,q)+1);}
   else {assert.ok(w>0&&h>0&&x+w<=480&&y+h<=400);area+=w*h;}
   const bytes=new Uint8Array(v.buffer,p+32,32),end=bytes.indexOf(0);assert.ok(end>=0);
   frame.push({kind,rgb,x,y,w,h,p:r,q,text:decoder.decode(bytes.subarray(0,end))});
  }
  assert.ok(area<=1000000,area);assert.equal(frame.at(-1).kind,4);frames++;return 0;
 }
};
async function reset(){instance=await WebAssembly.instantiate(wasm,{platform});instance.exports.app_init();}
function event(type,a,b=0){instance.exports.app_event(type,a,b);}
function tick(ms=200){now=(now+ms)>>>0;event(1,now);}
const has=s=>frame.some(c=>c.text===s);
await reset();assert.ok(has('--:--'));assert.ok(has('-- C'));assert.ok(has('请先设置城市'));
event(2,400,22);assert.ok(has('暂无天气'));event(2,40,150);
environment={flags:27,seconds:16*3600+28*60,temperature:245,weather:0,daylight:1,weather_age:0,location:'Example'};
tick();assert.ok(has('16:28'));assert.ok(has('24.5 C'));
fs.writeFileSync(root+'local/garden-day-frame.json',JSON.stringify({frame,background}));
event(3,1);assert.ok(has('你来啦'));
fs.writeFileSync(root+'local/garden-wind-frame.json',JSON.stringify({frame,background}));
tick(4200);assert.ok(!has('你来啦'));
event(3,1);assert.ok(!has('你来啦'),'motion cooldown');
event(3,2);assert.ok(has('你来啦'),'A bypasses motion cooldown');
tick(6000);event(3,3);assert.ok(has('开饭啦'));
tick(4500);event(2,190,265);assert.ok(has('摸摸好开心'));
event(2,230,376);event(2,470,330);assert.ok(frame.some(c=>c.rgb===0x6caeac));
tick(4200);assert.ok(!frame.some(c=>c.rgb===0x6caeac));
environment={...environment,daylight:0,weather:63,seconds:21*3600+8*60,flags:31};tick();assert.ok(has('21:08'));
fs.writeFileSync(root+'local/garden-night-frame.json',JSON.stringify({frame,background}));
event(2,400,22);assert.ok(has('天气缓存待更新'));event(2,40,150);
// Every animation/weather/input combination must fit the host's scene budget.
for(const code of [0,3,45,51,63,75,85,95]){
 environment.weather=code;
 for(const day of [0,1,-1]){environment.daylight=day;event(3,2);event(3,3);event(2,210,375);event(2,200,320);event(2,30,20);tick();event(2,30,20);tick(11000);}
}
// Regressions from physical feedback: distinct, time-varying poses and immediate watering.
environment.weather=0;environment.daylight=1;now=0;await reset();
const head=()=>frame.find(c=>c.rgb===0xf8eedb);
const eyes=()=>frame.filter(c=>c.rgb===0x4d5142).map(c=>c.h);
const resting={...head()};event(3,1);tick(600);assert.ok(head().y<resting.y,'arrival raises head');
const arrival={...head()};event(2,50,375);assert.ok(has('摸摸好开心'));assert.ok(eyes()[0]<=2,'pet closes eyes');
assert.deepEqual(sounds.at(-1),[2,1]);
const petPose={...head()};tick(400);assert.notEqual(head().x,petPose.x,'pet rubs rather than only changing caption');
event(3,3);assert.ok(has('开饭啦'));assert.ok(!has('摸摸好开心'));assert.ok(head().y>arrival.y,'feeding lowers head');
assert.deepEqual(sounds.at(-1),[2,2]);
const feedPose={...head()};tick(200);assert.notEqual(head().y,feedPose.y,'chewing nods');
const crumbs=frame.filter(c=>c.rgb===0xb38758).length;tick(1000);assert.ok(frame.filter(c=>c.rgb===0xb38758).length<crumbs);
event(2,220,375);assert.ok(frame.some(c=>c.rgb===0x78aaa5),'water button immediately shows watering can');
assert.ok(has('小花喝水啦')&&!has('开饭啦'),'watering feedback replaces previous feeding caption');
assert.ok(frame.some(c=>c.rgb===0x6caeac),'immediate droplets');assert.deepEqual(sounds.at(-1),[2,3]);
event(2,80,200);assert.ok(frame.some(c=>c.rgb===0x6caeac&&c.x<100),'upper garden accepts water target');
event(2,390,22);event(2,100,314);assert.deepEqual(sounds.at(-1),[1,0]);event(2,300,314);assert.equal(volume,75);assert.ok(has('音量：大'));event(2,300,314);assert.equal(volume,0);event(2,90,264);assert.equal(settingsRequests,1,'opens host settings');event(2,40,150);
// Exercise actions at their extremes, including changing mode during rain/wind.
for (const w of [0,63,75,95]) for(const d of [0,1]) {
 environment.weather=w;environment.daylight=d;
 for(const action of [0,1,2]) for(const age of [0,200,400,600,1000,2800,3800]) {
  event(3,2);event(2,40+action*160,375);tick(age);event(2,470,320);tick(200);
 }
}
now=0xffffff80;await reset();event(3,2);tick(4000);assert.ok(!has('你来啦'),'32-bit wrap look expiry');
assert.throws(()=>instance.exports.memory.grow(4),RangeError);
console.log(`PASS Garden WASM: unavailable/live/stale data, touch, A/B, motion cooldown, expiry, clock wrap, bounds; ${frames} frames, peak ${maxCommands}/96 commands`);

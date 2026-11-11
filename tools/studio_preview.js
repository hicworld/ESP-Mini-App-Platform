const {$, request, run, query, projectUrl} = Studio;
const name = query('name'), canvas = $('screen'), ctx = canvas.getContext('2d');
const hardwareNames = ['display','touch','speaker','motion','buttons','wifi','camera','microphone','temperature','humidity','light','imu','magnetometer','battery','haptic','led'];
const hardwareBits = Object.fromEntries(hardwareNames.map((item, index) => [item, 1 << index]));
const encoder = new TextEncoder(), decoder = new TextDecoder();
let manifest = null, app = null, timer = null, began = 0, store = new Map(), writes = 0, writeSecond = 0;
let sensorCalls = 0, feedbackCalls = 0, imageCalls = 0, sceneReady = false;
const currentMask = () => Number($('hardwareMask').value);
const color = rgb => '#' + (rgb >>> 0).toString(16).padStart(6, '0').slice(-6);
function stop() { if (timer) clearInterval(timer); clearTimeout(longTimer); pointerStart=null; timer = null; app = null; sceneReady=false; $('previewStatus').textContent = '已停止'; }
function memory(pointer, length) {
    if (!app || pointer < 0 || length < 0 || pointer + length > app.exports.memory.buffer.byteLength)
        throw Error('WASM 内存越界');
    return new Uint8Array(app.exports.memory.buffer, pointer, length);
}
function read(pointer, length) { return decoder.decode(memory(pointer, length)); }
function drawText(x, y, value, rgb) { ctx.fillStyle = color(rgb); ctx.font = '16px system-ui'; ctx.textBaseline = 'top'; ctx.fillText(value, x, y); }
function scene(pointer, count, background) {
    if (count < 0 || count > 96) throw Error('场景命令超限');
    ctx.fillStyle = color(background); ctx.fillRect(0, 0, 480, 400);
    const view = new DataView(memory(pointer, count * 64).buffer);
    for (let index = 0; index < count; index++) {
        const offset = pointer + index * 64, kind = view.getInt32(offset, true), rgb = view.getInt32(offset+4, true);
        const x = view.getInt32(offset+8, true), y = view.getInt32(offset+12, true);
        const w = view.getInt32(offset+16, true), h = view.getInt32(offset+20, true);
        const a = view.getInt32(offset+24, true), b = view.getInt32(offset+28, true);
        ctx.fillStyle = color(rgb); ctx.beginPath();
        if (kind === 1) { ctx.roundRect(x, y, w, h, Math.max(0, Math.min(a, w/2, h/2))); ctx.fill(); }
        else if (kind === 2) { ctx.ellipse(x+w/2, y+h/2, Math.abs(w/2), Math.abs(h/2), 0, 0, Math.PI*2); ctx.fill(); }
        else if (kind === 3) { ctx.moveTo(x,y); ctx.lineTo(w,h); ctx.lineTo(a,b); ctx.closePath(); ctx.fill(); }
        else if (kind === 4) { ctx.save(); ctx.beginPath(); ctx.rect(x,y,w,h); ctx.clip(); drawText(x,y,read(offset+32,32).split('\0')[0],rgb); ctx.restore(); }
    }
    sceneReady = true; return 0;
}
function image(x,y,w,h,pointer) {
    if (!(manifest?.permissions || []).includes('display') || ++imageCalls > 8) throw Error('图片权限或次数超限');
    if(!sceneReady) return -1;
    if(x<0||y<0||w<1||h<1||w>128||h>128||x+w>480||y+h>400) throw Error('图片尺寸无效');
    const source=memory(pointer,w*h*2), rgba=ctx.createImageData(w,h);
    for(let i=0;i<w*h;i++) {
        const pixel=source[i*2]|source[i*2+1]<<8, j=i*4;
        rgba.data[j]=Math.round(((pixel>>11)&31)*255/31);
        rgba.data[j+1]=Math.round(((pixel>>5)&63)*255/63);
        rgba.data[j+2]=Math.round((pixel&31)*255/31);
        rgba.data[j+3]=255;
    }
    ctx.putImageData(rgba,x,y); return 0;
}
function snapshot(permission,bit,pointer,capacity,size) {
    if(!(manifest?.permissions || []).includes(permission) || ++sensorCalls>4) throw Error('传感器权限或次数超限');
    if(capacity<size) throw Error('传感器缓冲区太小');
    if(!(currentMask()&hardwareBits[bit])) return null;
    const bytes=memory(pointer,size); bytes.fill(0);
    return new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
}
const numberValue=(id,scale=1)=>Math.round(Number($(id).value||0)*scale);
function imu(pointer,capacity) {
    const view=snapshot('sensors','imu',pointer,capacity,28); if(!view)return -1;
    ['imuAx','imuAy','imuAz'].forEach((id,i)=>view.setInt32(i*4,numberValue(id,1000),true));
    view.setUint32(24,Math.floor(performance.now()-began),true); return 28;
}
function orientation(pointer,capacity) {
    const view=snapshot('sensors','imu',pointer,capacity,20); if(!view)return -1;
    const ax=Number($('imuAx').value||0),ay=Number($('imuAy').value||0),az=Number($('imuAz').value||0);
    if(ax*ax+ay*ay+az*az<0.01)return -1;
    const roll=Math.atan2(ay,az),pitch=Math.atan2(-ax,Math.hypot(ay,az));
    view.setInt32(0,Math.round(roll*180000/Math.PI),true);
    view.setInt32(4,Math.round(pitch*180000/Math.PI),true);
    view.setInt32(8,-1,true);let flags=1;
    if(currentMask()&hardwareBits.magnetometer) {
        const mx=Number($('magnetX').value||0),my=Number($('magnetY').value||0),mz=Number($('magnetZ').value||0);
        const xh=mx*Math.cos(pitch)+mz*Math.sin(pitch);
        const yh=mx*Math.sin(roll)*Math.sin(pitch)+my*Math.cos(roll)-mz*Math.sin(roll)*Math.cos(pitch);
        if(xh*xh+yh*yh>0.01) {
            view.setInt32(8,(Math.round(Math.atan2(-yh,xh)*180000/Math.PI)+360000)%360000,true);
            flags|=2;
        }
    }
    view.setUint32(12,flags,true);
    view.setUint32(16,Math.floor(performance.now()-began),true);
    return 20;
}
function magnet(sensor,pointer,capacity) {
    const view=snapshot('sensors','magnetometer',pointer,capacity,16); if(!view||sensor<0||sensor>1)return -1;
    ['magnetX','magnetY','magnetZ'].forEach((id,i)=>view.setInt32(i*4,numberValue(id,100),true));
    view.setUint32(12,Math.floor(performance.now()-began),true); return 16;
}
function battery(pointer,capacity) {
    const view=snapshot('sensors','battery',pointer,capacity,16); if(!view)return -1;
    view.setInt32(0,3800,true); view.setInt32(4,0,true);
    view.setInt32(8,Math.max(0,Math.min(100,numberValue('batteryPercent'))),true); return 16;
}
function feedback(kind,intensity,duration) {
    if(!(manifest?.permissions || []).includes('feedback') || ++feedbackCalls>4) throw Error('反馈权限或次数超限');
    if(intensity<0||intensity>100||duration<0||duration>1000||![1,2].includes(kind))return -2;
    if(!(currentMask()&hardwareBits[kind===1?'haptic':'led']))return -1;
    $('feedbackState').textContent=`${kind===1?'震动':'LED'}：${intensity}%，${duration} ms（仅模拟）`;
    return 0;
}
function environment(pointer, capacity) {
    if (capacity < 56) return -1;
    const bytes = memory(pointer, 56); bytes.fill(0);
    const view = new DataView(bytes.buffer), now = new Date();
    const seconds = now.getHours()*3600 + now.getMinutes()*60;
    const temperature = Math.round(Number($('temperature').value || 25)*10), weather = Number($('weather').value || 0);
    [27,seconds,temperature,weather,1,0].forEach((value,index) => view.setInt32(pointer+index*4,value,true));
    bytes.set(encoder.encode('PC Preview').subarray(0,31),24); return 56;
}
const key = (pointer,length) => read(pointer,length);
function kvGet(kp,kn,vp,capacity) {
    const value = store.get(key(kp,kn)); if (!value) return 0;
    if (capacity < value.length) return -2;
    memory(vp,value.length).set(value); return value.length;
}
function kvSet(kp,kn,vp,length) {
    const name = key(kp,kn);
    if (!/^[A-Za-z0-9_-]{1,15}$/.test(name) || name.startsWith('__') || length < 0 || length > 128) return -1;
    const value = memory(vp,length).slice(), old = store.get(name);
    if (old && old.length === value.length && old.every((item,index) => item === value[index])) return 0;
    if (!old && store.size >= 8) return -3;
    const second = Math.floor(performance.now()/1000);
    if (second !== writeSecond) { writeSecond = second; writes = 0; }
    if (writes >= 4) return -3;
    writes++; store.set(name,value); return 0;
}
function missingRequired() { return (manifest?.hardware || []).filter(item => !(currentMask() & hardwareBits[item])); }
function unavailable(missing) {
    stop(); const message = '无法预览：缺少必需外设 ' + missing.join('、');
    ctx.fillStyle = '#f4f2e9'; ctx.fillRect(0,0,480,400); ctx.fillStyle = '#a32424';
    ctx.font = '18px system-ui'; ctx.fillText(message,20,70); $('previewStatus').textContent = message; return message;
}
function event(type,a,b=0) {
    if (!app) return;
    const mask = currentMask();
    if ([2,4,5,6,7,8].includes(type) && (!(mask & hardwareBits.touch) || !(manifest?.permissions || []).includes('touch'))) return;
    if (type === 3 && (!(manifest?.permissions || []).includes('input') || (a === 1 && !(mask & hardwareBits.motion)) || ((a === 2 || a === 3) && !(mask & hardwareBits.buttons)))) return;
    try { sensorCalls=feedbackCalls=imageCalls=0; app.exports.app_event(type,a,b); }
    catch (error) { stop(); $('previewStatus').textContent = '预览错误：' + error.message; }
}
async function start() {
    if (!manifest) throw Error('项目尚未加载');
    stop(); const missing = missingRequired(); if (missing.length) throw Error(unavailable(missing));
    const response = await request('/api/wasm?name=' + encodeURIComponent(name)), bytes = await response.arrayBuffer();
    began = performance.now();
    const host = {
        clear: rgb => { sceneReady=false; ctx.fillStyle = color(rgb); ctx.fillRect(0,0,480,400); },
        rect: (x,y,w,h,rgb) => { ctx.fillStyle = color(rgb); ctx.fillRect(x,y,w,h); },
        text: (x,y,p,n,rgb) => drawText(x,y,read(p,n),rgb),
        millis: () => Math.floor(performance.now()-began) >>> 0,
        hardware: currentMask,
        settings: () => { $('previewStatus').textContent = '设备会打开系统设置；电脑预览不修改设置'; },
        audio: (kind,value) => kind === 3 ? value : 0,
        scene, image, environment, kv_get:kvGet, kv_set:kvSet,
        imu, orientation, magnet, battery, feedback,
    };
    app = (await WebAssembly.instantiate(bytes,{platform:host})).instance;
    store = new Map(); writes = 0; writeSecond = 0; sensorCalls=feedbackCalls=imageCalls=0; sceneReady=false;
    try {
        app.exports.app_init(); timer = setInterval(() => event(1,host.millis()),50);
        $('previewStatus').textContent = currentMask() & hardwareBits.touch ? '预览运行中 · 可测试按下、移动、长按、滑动' : '预览运行中 · 当前无触摸外设';
    } catch (error) { stop(); throw error; }
}

$('preview').onclick = () => run(start,'previewStatus'); $('stop').onclick = stop;
$('hardwareMask').onchange = () => {
    if (!manifest) return;
    const missing = missingRequired(); if (missing.length) { unavailable(missing); return; }
    $('previewStatus').textContent = app ? '预览运行中 · 模拟外设已更新' : '模拟外设已更新；请启动预览';
};
for (const button of document.querySelectorAll('[data-event]')) button.onclick = () => event(3,Number(button.dataset.event));
const point=pointer=>{const box=canvas.getBoundingClientRect();return [
    Math.max(0,Math.min(479,Math.floor((pointer.clientX-box.left)*480/box.width))),
    Math.max(0,Math.min(399,Math.floor((pointer.clientY-box.top)*400/box.height)))];};
let pointerStart=null, lastMove=0, longTimer=null, longSent=false;
canvas.addEventListener('pointerdown',pointer=>{
    if(!app)return; canvas.setPointerCapture(pointer.pointerId);
    const [x,y]=point(pointer); pointerStart={x,y,time:performance.now()}; longSent=false;
    event(4,x,y);
    longTimer=setTimeout(()=>{if(pointerStart&&!longSent){longSent=true;event(7,x,y);}},600);
});
canvas.addEventListener('pointermove',pointer=>{
    if(!pointerStart)return;
    const [x,y]=point(pointer);
    if(Math.abs(x-pointerStart.x)>=16||Math.abs(y-pointerStart.y)>=16)clearTimeout(longTimer);
    const now=performance.now();if(now-lastMove>=30){lastMove=now;event(5,x,y);}
});
function finishPointer(pointer,cancel=false){
    if(!pointerStart)return;clearTimeout(longTimer);
    const [x,y]=point(pointer),dx=x-pointerStart.x,dy=y-pointerStart.y;
    event(6,x,y);
    if(!cancel) {
        if(Math.abs(dx)>=45||Math.abs(dy)>=45)event(8,Math.abs(dx)>=Math.abs(dy)?(dx<0?1:2):(dy<0?3:4),Math.max(Math.abs(dx),Math.abs(dy)));
        else if(!longSent)event(2,x,y);
    }
    pointerStart=null;
}
canvas.addEventListener('pointerup',pointer=>finishPointer(pointer));
canvas.addEventListener('pointercancel',pointer=>finishPointer(pointer,true));
window.addEventListener('pagehide', stop);
run(async () => {
    if (!name) throw Error('缺少项目名称，请从项目列表进入');
    $('editLink').href = projectUrl('project',name); $('detailLink').href = projectUrl('app',name);
    const project = await request('/api/project?name=' + encodeURIComponent(name));
    manifest = project.manifest; $('pageTitle').textContent = project.manifest.name + ' · 电脑预览';
    if (!project.built) $('previewStatus').textContent = '尚未构建，请返回项目开发页构建';
},'previewStatus');

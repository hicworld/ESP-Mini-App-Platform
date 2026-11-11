const {$, request, run, status, query, projectUrl} = Studio;
const permissionNames = ['display','touch','storage','environment','input','audio','sensors','feedback'];
const hardwareNames = ['display','touch','speaker','motion','buttons','wifi','camera','microphone','temperature','humidity','light','imu','magnetometer','battery','haptic','led'];
const name = query('name'), encoder = new TextEncoder();
let manifest = null, built = false, savedManifest = '', editorPath = '', editorHash = '', editorOriginal = '';

function boxes(id, names) {
    $(id).replaceChildren(...names.map(item => {
        const label = document.createElement('label'), box = document.createElement('input');
        box.type = 'checkbox'; box.value = item; label.append(box, document.createTextNode(' ' + item)); return label;
    }));
}
function values(id) { return [...$(id).querySelectorAll('input:checked')].map(box => box.value); }
function setValues(id, items) { for (const box of $(id).querySelectorAll('input')) box.checked = items.includes(box.value); }
function formManifest() {
    const result = {...manifest, id:$('appId').value.trim(), name:$('appName').value.trim(), version:$('version').value.trim(),
        platform_min:$('platformMin').value.trim(), permissions:values('permissions'), hardware:values('hardware'),
        optional_hardware:values('optional_hardware')};
    const author = $('author').value.trim(), description = $('description').value.trim();
    if (encoder.encode(author).length > 80) throw Error('作者名不能超过 80 个 UTF-8 字节');
    if (encoder.encode(description).length > 500) throw Error('应用简介不能超过 500 个 UTF-8 字节');
    if (author) result.author = author; else delete result.author;
    if (description) result.description = description; else delete result.description;
    return result;
}
function formDirty() { return manifest && JSON.stringify(formManifest()) !== savedManifest; }
function editorDirty() { return editorPath && $('editor').value !== editorOriginal; }
function setSummary() { $('projectSummary').textContent = `${name} · ${built ? '已有构建' : '未构建'} · 修改源码或 Manifest 后请重新构建`; }
function fill(project) {
    manifest = project.manifest; built = project.built;
    $('pageTitle').textContent = project.manifest.name;
    $('appId').value = manifest.id; $('appName').value = manifest.name; $('version').value = manifest.version;
    $('platformMin').value = manifest.platform_min || '0.1.0'; $('author').value = manifest.author || '';
    $('description').value = manifest.description || '';
    for (const id of ['permissions','hardware','optional_hardware']) setValues(id, manifest[id] || []);
    savedManifest = JSON.stringify(formManifest()); setSummary();
}
async function saveManifest() {
    const project = await request('/api/manifest', 'POST', {name, manifest:formManifest()});
    fill(project); return project;
}
async function listFiles() {
    const files = await request('/api/files?name=' + encodeURIComponent(name));
    $('editorFiles').replaceChildren(...files.map(path => {
        const button = document.createElement('button'); button.textContent = path;
        button.className = path === editorPath ? 'active' : '';
        button.onclick = () => run(() => openFile(path)); return button;
    }));
    if (!files.length) $('editorFiles').textContent = '暂无可编辑的文本源码';
}
async function openFile(path) {
    if (editorDirty() && !confirm('当前代码尚未保存。确定放弃修改并打开其他文件吗？')) return;
    const data = await request('/api/file?name=' + encodeURIComponent(name) + '&path=' + encodeURIComponent(path));
    editorPath = data.path; editorHash = data.sha256; editorOriginal = data.content;
    $('editorPath').textContent = data.path; $('editor').value = data.content;
    $('editor').disabled = false; $('saveFile').disabled = false; $('editorStatus').textContent = '已打开';
    await listFiles();
}

$('save').onclick = () => run(async () => { await saveManifest(); status('status', 'Manifest 已保存；请重新构建'); });
$('build').onclick = () => run(async () => {
    if (editorDirty()) throw Error('源码有未保存的修改；请先保存代码');
    $('build').disabled = true; status('status', '正在保存、构建和检查…');
    try {
        await saveManifest();
        const result = await request('/api/build', 'POST', {name});
        $('log').textContent = result.log;
        const project = await request('/api/project?name=' + encodeURIComponent(name)); fill(project);
        status('status', result.ok ? '构建和 ABI 检查通过；现在可以进入电脑预览' : '构建或检查失败，请查看日志', !result.ok);
    } finally { $('build').disabled = false; }
});
$('export').onclick = () => run(async () => {
    if (formDirty() || editorDirty()) throw Error('请先保存修改并重新构建，再导出投稿包');
    const response = await request('/api/export?name=' + encodeURIComponent(name));
    const blob = await response.blob(), url = URL.createObjectURL(blob), a = document.createElement('a');
    a.href = url; a.download = (response.headers.get('Content-Disposition') || '').match(/filename="([^"]+)"/)?.[1] || 'submission.zip';
    a.click(); setTimeout(() => URL.revokeObjectURL(url), 30000); status('status', '已导出无签名投稿 ZIP');
});
$('saveFile').onclick = () => run(async () => {
    if (!editorPath) return;
    const result = await request('/api/file', 'POST', {name, path:editorPath, content:$('editor').value, sha256:editorHash});
    editorHash = result.sha256; editorOriginal = $('editor').value; built = false; setSummary();
    $('editorStatus').textContent = '已保存，请重新构建'; status('status', '代码已保存；请重新构建');
});
$('editor').oninput = () => { $('editorStatus').textContent = editorDirty() ? '有未保存的修改' : '已保存'; };
for (const [id, message] of [['previewLink','请先保存修改并重新构建，再进入预览'],['detailLink','请先保存 Manifest 和代码，再查看详情']]) {
    $(id).onclick = event => { if (formDirty() || editorDirty()) { event.preventDefault(); status('status', message, true); } };
}
window.addEventListener('beforeunload', event => {
    if (editorDirty() || formDirty()) { event.preventDefault(); event.returnValue = ''; }
});

run(async () => {
    if (!name) throw Error('缺少项目名称，请返回项目列表');
    $('previewLink').href = projectUrl('preview', name); $('detailLink').href = projectUrl('app', name);
    boxes('permissions', permissionNames); boxes('hardware', hardwareNames); boxes('optional_hardware', hardwareNames);
    const project = await request('/api/project?name=' + encodeURIComponent(name)); fill(project); await listFiles();
});

const {$, session, request, run, status, projectName, projectUrl} = Studio;

async function showProjects() {
    const projects = await request('/api/projects');
    $('projects').replaceChildren(...projects.map(project => {
        const row = document.createElement('div'), info = document.createElement('div'), title = document.createElement('strong'), sub = document.createElement('small'), actions = document.createElement('div');
        row.className = 'project-row'; info.className = 'info'; actions.className = 'actions';
        title.textContent = project.manifest.name; sub.textContent = `${project.project} · ${project.language.toUpperCase()} · ${project.built ? '已有构建' : '未构建'}`;
        info.append(title, sub);
        for (const [label, page] of [['开发', 'project'], ['预览', 'preview'], ['详情', 'app']]) {
            const link = document.createElement('a'); link.className = 'button light'; link.href = projectUrl(page, project.project); link.textContent = label; actions.append(link);
        }
        const remove = document.createElement('button'); remove.className = 'danger'; remove.textContent = '移除';
        remove.setAttribute('aria-label', `移除 ${project.manifest.name} · ${project.project}`);
        remove.onclick = () => run(async () => {
            if (!confirm(`将“${project.manifest.name} · ${project.project}”移到“最近移除”？\n可恢复；原始导入文件夹和已导出的投稿 ZIP 不受影响。`)) return;
            await request('/api/remove', 'POST', {name: project.project});
            await Promise.all([showProjects(), showRemoved()]);
            status('projectStatus', `已移除 ${project.project}，可在“最近移除”恢复`);
        }, 'projectStatus');
        actions.append(remove); row.append(info, actions); return row;
    }));
    if (!projects.length) $('projects').textContent = '暂无项目。可以创建模板或从示例复制。';
}

async function showRemoved() {
    const entries = await request('/api/removed');
    $('removedProjects').replaceChildren(...entries.map(entry => {
        const row = document.createElement('div'), info = document.createElement('div'), title = document.createElement('strong'), button = document.createElement('button');
        row.className = 'removed-row'; info.className = 'info'; title.textContent = `${entry.title} · ${entry.name}`;
        button.className = 'light'; button.textContent = '恢复';
        button.onclick = () => run(async () => {
            const restored = await request('/api/restore', 'POST', {id: entry.id});
            await Promise.all([showProjects(), showRemoved()]);
            status('projectStatus', `已恢复 ${restored.project}`);
        }, 'projectStatus');
        info.append(title); row.append(info, button); return row;
    }));
    if (!entries.length) $('removedProjects').textContent = '暂无已移除项目';
}

$('create').onclick = () => run(async () => {
    const name = projectName($('newName').value.trim());
    const result = await request('/api/create', 'POST', {name});
    location.href = projectUrl('project', result.project);
}, 'projectStatus');

$('import').onclick = () => { $('directoryPicker').value = ''; $('directoryPicker').click(); };
$('directoryPicker').onchange = async () => {
    const files = [...$('directoryPicker').files]; if (!files.length) return;
    const paths = files.map(file => file.webkitRelativePath), folder = paths[0].split('/')[0];
    $('import').disabled = true; status('importStatus', `正在导入 ${folder}…`);
    try {
        if (files.length > 500) throw Error('文件数量超过 500 个');
        if (!paths.includes(folder + '/manifest.json')) throw Error('所选文件夹根目录必须包含 manifest.json');
        const name = projectName($('importName').value.trim() || folder), form = new FormData();
        form.append('meta', JSON.stringify({name, paths}));
        files.forEach((file, index) => form.append(String(index), file, file.name));
        const current = await session;
        const response = await fetch('/api/import-files', {method:'POST', headers:{'X-Studio-Token':current.token}, body:form});
        const result = await response.json(); if (!response.ok) throw Error(result.error || `HTTP ${response.status}`);
        location.href = projectUrl('project', result.project);
    } catch (error) { status('importStatus', '导入失败：' + error.message, true); }
    finally { $('import').disabled = false; }
};

$('diagnose').onclick = () => run(async () => {
    const checks = await request('/api/diagnostics');
    $('diagnostics').replaceChildren(...checks.map(check => {
        const row = document.createElement('div'), title = document.createElement('strong'), detail = document.createElement('div');
        row.className = 'diagnostic ' + (check.ok ? 'ok' : 'fail');
        title.textContent = (check.ok ? '✓ ' : '✗ ') + check.name; detail.textContent = check.detail; row.append(title, detail);
        if (!check.ok && check.fix) { const fix = document.createElement('code'); fix.textContent = '建议：' + check.fix; row.append(fix); }
        return row;
    }));
}, 'projectStatus');

run(async () => {
    const current = await session;
    $('workspace').textContent = current.workspace;
    if (!(current.capabilities || []).includes('pages_v1')) throw Error('本地服务是旧版本。请停止并重新运行 python tools/developer_studio.py');
    await Promise.all([showProjects(), showRemoved()]);
}, 'projectStatus');

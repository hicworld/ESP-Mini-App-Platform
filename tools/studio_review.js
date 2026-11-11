const {$, session, status} = Studio;
let lastReport;

$('check').onclick = async () => {
    const file = $('package').files[0];
    if (!file) { status('status', '请先选择投稿 ZIP', true); return; }
    if (file.size > 12 * 1024 * 1024) { status('status', 'ZIP 超过 12 MiB', true); return; }
    $('check').disabled = true;
    $('download').disabled = true;
    $('result').hidden = true;
    status('status', `正在检查 ${file.name}…`);
    try {
        const current = await session;
        if (!(current.capabilities || []).includes('submission_review_v1'))
            throw Error('本地服务是旧版本，请重启工作台后刷新页面');
        const response = await fetch('/api/review-upload', {
            method: 'POST',
            headers: {'Content-Type': 'application/zip', 'X-Studio-Token': current.token,
                      'X-Submission-Name': encodeURIComponent(file.name)},
            body: await file.arrayBuffer()
        });
        const report = await response.json();
        if (!response.ok) throw Error(report.error || `HTTP ${response.status}`);
        lastReport = report;
        $('result').hidden = false;
        $('verdict').textContent = report.status === 'REJECTED' ? '拒收：需要作者修正投稿包' : '进入人工复核';
        $('fileName').textContent = file.name;
        $('fileHash').textContent = report.zip_sha256;
        $('appInfo').textContent = report.manifest ?
            `${report.manifest.name} · ${report.manifest.id} · ${report.manifest.version}` : '无法读取';
        $('counts').textContent = `失败 ${report.counts.error} · 待复核 ${report.counts.warning} · 通过 ${report.counts.pass}`;
        $('findings').replaceChildren(...report.findings.map(item => {
            const row = document.createElement('div'), title = document.createElement('strong'), detail = document.createElement('div');
            row.className = 'diagnostic ' + (item.level === 'error' ? 'fail' : item.level === 'pass' ? 'ok' : '');
            title.textContent = `${{error:'✗ 失败', warning:'! 待复核', pass:'✓ 通过'}[item.level]} · ${item.code}`;
            detail.textContent = item.detail;
            row.append(title, detail); return row;
        }));
        $('download').disabled = false;
        status('status', `已检查 ${file.name}；${report.status === 'REJECTED' ? '拒收' : '请继续人工复核'}`,
               report.status === 'REJECTED');
    } catch (error) { status('status', '检查失败：' + error.message, true); }
    finally { $('check').disabled = false; }
};

$('download').onclick = () => {
    if (!lastReport) return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(lastReport, null, 2)], {type:'application/json'}));
    const link = document.createElement('a');
    link.href = url; link.download = 'submission-review.json'; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
};

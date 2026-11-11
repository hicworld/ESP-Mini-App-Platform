/* Shared browser utilities for the local-only Developer Studio pages. */
window.Studio = (() => {
    const $ = id => document.getElementById(id);
    const session = fetch('/api/session').then(async response => {
        if (!response.ok) throw Error('无法连接本地工作台服务；请重新启动后刷新页面');
        return response.json();
    });
    async function request(url, method='GET', body) {
        const current = await session;
        const headers = {'X-Studio-Token': current.token};
        if (body !== undefined) headers['Content-Type'] = 'application/json';
        const response = await fetch(url, {method, headers, body: body === undefined ? undefined : JSON.stringify(body)});
        if (!response.ok) {
            const data = await response.json().catch(() => ({}));
            throw Error(data.error || `HTTP ${response.status}`);
        }
        return (response.headers.get('Content-Type') || '').includes('application/json') ? response.json() : response;
    }
    function status(id, message, error=false) {
        const element = $(id);
        if (!element) return;
        element.textContent = message;
        element.classList.toggle('error', error);
    }
    async function run(task, statusId='status') {
        try { return await task(); }
        catch (error) { status(statusId, error.message, true); }
    }
    function query(name) { return new URLSearchParams(location.search).get(name) || ''; }
    function projectUrl(page, name) { return `/${page}?name=${encodeURIComponent(name)}`; }
    function projectName(raw) {
        let name = raw.toLowerCase().replace(/[^a-z0-9_-]+/g, '_').replace(/^[_-]+|[_-]+$/g, '');
        if (!name) name = 'app_' + Date.now().toString(36).slice(-6);
        if (!/^[a-z]/.test(name) || name.length < 2) name = 'app_' + name;
        return name.slice(0, 40);
    }
    function appendInline(parent, value, sourcePath, docs) {
        const pattern = /`([^`]+)`|\[([^\]]+)\]\(([^)]+)\)|\*\*([^*]+)\*\*/g;
        let start = 0, match;
        while ((match = pattern.exec(value))) {
            parent.append(document.createTextNode(value.slice(start, match.index)));
            if (match[1] !== undefined) {
                const code = document.createElement('code'); code.textContent = match[1]; parent.append(code);
            } else if (match[4] !== undefined) {
                const strong = document.createElement('strong'); strong.textContent = match[4]; parent.append(strong);
            } else {
                let target;
                try {
                    if (!/^[a-z]+:/i.test(match[3]) && !match[3].startsWith('//'))
                        target = new URL(match[3], 'https://docs.invalid/' + sourcePath).pathname.slice(1);
                } catch {}
                const linked = docs.find(item => item.path === target);
                if (linked) {
                    const a = document.createElement('a'); a.href = '/docs?id=' + encodeURIComponent(linked.id);
                    a.textContent = match[2]; parent.append(a);
                } else parent.append(document.createTextNode(match[2]));
            }
            start = pattern.lastIndex;
        }
        parent.append(document.createTextNode(value.slice(start)));
    }
    function renderMarkdown(root, markdown, sourcePath, docs) {
        root.replaceChildren();
        const lines = markdown.replace(/\r\n/g, '\n').split('\n');
        const cells = line => line.trim().replace(/^\||\|$/g, '').split('|').map(item => item.trim());
        const separator = line => /^\s*\|?[\s:|-]+\|?\s*$/.test(line) && line.includes('-');
        let i = 0;
        while (i < lines.length) {
            const line = lines[i];
            if (!line.trim()) { i++; continue; }
            if (/^\s*```/.test(line)) {
                const pre = document.createElement('pre'), code = document.createElement('code'), body = [];
                i++; while (i < lines.length && !/^\s*```/.test(lines[i])) body.push(lines[i++]);
                if (i < lines.length) i++;
                code.textContent = body.join('\n'); pre.append(code); root.append(pre); continue;
            }
            const heading = line.match(/^(#{1,3})\s+(.+)$/);
            if (heading) {
                const h = document.createElement('h' + heading[1].length);
                appendInline(h, heading[2], sourcePath, docs); root.append(h); i++; continue;
            }
            if (line.includes('|') && i + 1 < lines.length && separator(lines[i+1])) {
                const table = document.createElement('table'), thead = document.createElement('thead'), tr = document.createElement('tr');
                for (const cell of cells(line)) { const th = document.createElement('th'); appendInline(th, cell, sourcePath, docs); tr.append(th); }
                thead.append(tr); table.append(thead); i += 2;
                const tbody = document.createElement('tbody');
                while (i < lines.length && lines[i].trim().startsWith('|')) {
                    const row = document.createElement('tr');
                    for (const cell of cells(lines[i++])) { const td = document.createElement('td'); appendInline(td, cell, sourcePath, docs); row.append(td); }
                    tbody.append(row);
                }
                table.append(tbody); root.append(table); continue;
            }
            const list = line.match(/^\s*(-|\d+\.)\s+(.+)$/);
            if (list) {
                const ordered = list[1] !== '-', group = document.createElement(ordered ? 'ol' : 'ul');
                while (i < lines.length) {
                    const item = lines[i].match(/^\s*(-|\d+\.)\s+(.+)$/);
                    if (!item || (item[1] !== '-') !== ordered) break;
                    const li = document.createElement('li'); appendInline(li, item[2], sourcePath, docs); group.append(li); i++;
                }
                root.append(group); continue;
            }
            const paragraph = document.createElement('p'), parts = [];
            while (i < lines.length && lines[i].trim() && !/^(#{1,3})\s+|^\s*```|^\s*(-|\d+\.)\s+/.test(lines[i]) &&
                   !(lines[i].includes('|') && i + 1 < lines.length && separator(lines[i+1]))) parts.push(lines[i++].trim());
            if (!parts.length) { i++; continue; }
            appendInline(paragraph, parts.join(' '), sourcePath, docs); root.append(paragraph);
        }
    }
    return {$, session, request, status, run, query, projectUrl, projectName, renderMarkdown};
})();

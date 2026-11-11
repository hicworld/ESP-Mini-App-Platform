const {$, request, run, query, renderMarkdown} = Studio;
run(async () => {
    const docs = await request('/api/docs');
    $('docLinks').replaceChildren(...docs.map(doc => {
        const link = document.createElement('a'); link.className = 'button light';
        link.style.display = 'block'; link.style.marginBottom = '7px';
        link.href = '/docs?id=' + encodeURIComponent(doc.id); link.textContent = doc.title;
        if (doc.id === query('id')) link.setAttribute('aria-current','page');
        return link;
    }));
    if (!query('id')) {
        const heading = document.createElement('h2'), paragraph = document.createElement('p');
        heading.textContent = '从这里开始';
        paragraph.textContent = '先读“第三方应用代码规范与构建指南”，写代码时随时查看“平台函数速查”。';
        $('docContent').replaceChildren(heading, paragraph); return;
    }
    const doc = await request('/api/doc?id=' + encodeURIComponent(query('id')));
    document.title = doc.title + ' · ESP Mini App';
    renderMarkdown($('docContent'),doc.markdown,doc.path,docs);
},'docContent');

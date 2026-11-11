const {$, request, run, status, query, projectUrl} = Studio;
const exampleId = query('id');
async function renderList() {
    const examples = await request('/api/examples');
    $('exampleCards').replaceChildren(...examples.map(example => {
        const card = document.createElement('section'), title = document.createElement('h3'), summary = document.createElement('p');
        const tag = document.createElement('span'), link = document.createElement('a');
        card.className = 'example-card'; title.textContent = example.title;
        summary.textContent = example.summary; tag.className = 'tag'; tag.textContent = example.language;
        link.className = 'button light'; link.href = '/examples?id=' + encodeURIComponent(example.id); link.textContent = '查看功能与代码';
        card.append(title, tag, summary, link); return card;
    }));
}
async function renderDetail() {
    const example = await request('/api/example?id=' + encodeURIComponent(exampleId));
    $('exampleList').hidden = true; $('exampleDetail').hidden = false;
    $('exampleTitle').textContent = example.title; $('exampleSummary').textContent = example.summary;
    $('exampleLanguage').textContent = example.language; $('exampleTry').textContent = example.try;
    $('exampleFeatures').replaceChildren(...example.features.map(feature => {
        const li = document.createElement('li'); li.textContent = feature; return li;
    }));
    function showFile(index) {
        $('exampleCode').textContent = example.files[index].content;
        for (const button of $('exampleFiles').querySelectorAll('button')) button.classList.toggle('active',Number(button.dataset.index) === index);
    }
    $('exampleFiles').replaceChildren(...example.files.map((file,index) => {
        const button = document.createElement('button'); button.textContent = file.path;
        button.dataset.index = index; button.onclick = () => showFile(index); return button;
    }));
    showFile(0);
    $('copyExample').onclick = () => run(async () => {
        $('copyExample').disabled = true; status('exampleStatus','正在复制示例…');
        try {
            const result = await request('/api/example-copy','POST',{id:exampleId});
            location.href = projectUrl('project',result.project);
        } finally { $('copyExample').disabled = false; }
    },'exampleStatus');
    document.title = example.title + ' · 示例应用';
}
run(() => exampleId ? renderDetail() : renderList(),'exampleStatus');

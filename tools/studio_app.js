const {$, request, run, query, projectUrl} = Studio;
const name = query('name');
(async () => { try {
    if (!name) throw Error('缺少项目名称，请从项目列表进入');
    $('editLink').href = projectUrl('project',name); $('previewLink').href = projectUrl('preview',name);
    const project = await request('/api/project?name=' + encodeURIComponent(name)), data = project.manifest;
    const fields = {detailName:data.name, detailAuthor:data.author || '未填写',
        detailDescription:data.description || '作者尚未填写应用简介。', detailId:data.id,
        detailVersion:data.version, detailPlatform:data.platform_min || '0.1.0',
        detailBuild:project.built ? '已有构建' : '未构建',
        detailPermissions:(data.permissions || []).join('、') || '无',
        detailHardware:(data.hardware || []).join('、') || '无',
        detailOptional:(data.optional_hardware || []).join('、') || '无'};
    for (const [id,value] of Object.entries(fields)) $(id).textContent = value;
} catch (error) { $('detailName').textContent = error.message; } })();

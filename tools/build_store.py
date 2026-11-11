"""Build a static app directory from verified signed packages; no private key is published."""
import argparse
import hashlib
import html
import json
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from app_package import verify

ROOT = Path(__file__).resolve().parents[1]


def publish(packages, public_key, output, base_url=None):
    if base_url:
        if not base_url.startswith('https://'):
            raise ValueError('Public base URL must use HTTPS')
        base_url = base_url.rstrip('/')
    def link(path):
        return f'{base_url}/{path}' if base_url else path
    esc = lambda value: html.escape(str(value), quote=True)
    if len(packages)>12:
        raise ValueError('Directory supports at most 12 apps')
    prepared, ids = [], set()
    for path in packages:
        payload = path.read_bytes()
        manifest, _ = verify(payload, public_key)
        if manifest['id'] in ids:
            raise ValueError('Only one version of each app is allowed')
        ids.add(manifest['id'])
        digest = hashlib.sha256(payload).hexdigest()
        # Content-addressed filenames keep the previous index usable until replaced.
        filename = f'apps/{digest}.app'
        entry = {key: manifest[key] for key in ('id','name','version','permissions')}
        entry.update(file=filename, size=len(payload), sha256=digest,
                     platform_min=manifest.get('platform_min','0.1.0'))
        prepared.append((entry,payload,manifest))
    index = {'schema_version':1,'apps':[entry for entry,_,_ in prepared]}
    encoded = (json.dumps(index,ensure_ascii=False,indent=2)+'\n').encode('utf-8')
    if len(encoded)>20*1024:
        raise ValueError('Index exceeds device limit')
    output.mkdir(parents=True,exist_ok=True)
    (output/'apps').mkdir(exist_ok=True)
    (output/'details').mkdir(exist_ok=True)
    rows = []
    for entry,payload,manifest in prepared:
        (output/entry['file']).write_bytes(payload)
        author = manifest.get('author') or '未填写'
        description = manifest.get('description') or '作者尚未提供应用简介。'
        detail_name = f'{entry["sha256"]}.html'
        rows.append(f'<article><h2><a href="{esc(link(f"details/{detail_name}"))}">{esc(entry["name"])}</a></h2>'
                    f'<p>作者：{esc(author)}</p><p>{esc(entry["id"])} · {esc(entry["version"])}</p></article>')
        return_href = link('index.html') if base_url else '../index.html'
        download_href = link(entry['file']) if base_url else '../' + entry['file']
        detail = ('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
                  f'<title>{esc(entry["name"])} · 应用详情</title>'
                  '<style>body{font:17px/1.6 system-ui;background:#f4f2e9;color:#202923;max-width:860px;margin:48px auto;padding:0 24px}'
                  'article{background:white;border:1px solid #d8ded4;border-radius:16px;padding:28px}a{color:#405c42}'
                  'h1{font-size:32px}dt{color:#667166;font-size:14px}dd{margin:2px 0 16px;overflow-wrap:anywhere}.description{white-space:pre-wrap}</style>'
                  f'<p><a href="{esc(return_href)}">← 返回应用目录</a></p><article><h1>{esc(entry["name"])}</h1>'
                  f'<p><strong>作者：{esc(author)}</strong></p><p class="description">{esc(description)}</p><dl>'
                  f'<dt>应用 ID</dt><dd>{esc(entry["id"])}</dd><dt>版本</dt><dd>{esc(entry["version"])}</dd>'
                  f'<dt>最低平台版本</dt><dd>{esc(entry["platform_min"])}</dd>'
                  f'<dt>权限</dt><dd>{esc(", ".join(entry["permissions"]) or "无")}</dd>'
                  f'<dt>必需外设</dt><dd>{esc(", ".join(manifest["hardware"]) or "无")}</dd>'
                  f'<dt>可选外设</dt><dd>{esc(", ".join(manifest.get("optional_hardware", [])) or "无")}</dd>'
                  f'<dt>包大小</dt><dd>{entry["size"]} 字节</dd><dt>SHA256</dt><dd>{entry["sha256"]}</dd></dl>'
                  f'<p><a href="{esc(download_href)}" download="{esc(Path(entry["file"]).name)}">下载已签名应用包</a></p></article></html>')
        (output/'details'/detail_name).write_text(detail,encoding='utf-8')
    page = ('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>ESP Mini App 应用目录</title><style>body{font:17px system-ui;background:#101d32;color:#e9eff8;max-width:840px;margin:60px auto;padding:0 24px}'
            'article{background:#233a53;padding:24px;margin:18px 0;border-radius:16px}a{color:#63dfbd}h1{font-size:36px}p{line-height:1.5}</style>'
            f'<h1>ESP Mini App 应用目录</h1><p>已签名应用。设备使用 <a href="{esc(link("index.json"))}">index.json</a> 获取目录。</p>'
            + ''.join(rows) + '</html>')
    (output/'index.html').write_text(page,encoding='utf-8')
    temporary=output/'index.json.tmp'; temporary.write_bytes(encoded)
    temporary.replace(output/'index.json')
    return index


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packages',type=Path,nargs='+')
    parser.add_argument('--output',type=Path,default=ROOT/'dist/store')
    parser.add_argument('--public-key',type=Path,default=ROOT/'firmware/main/trusted_key.pem')
    parser.add_argument('--base-url',help='Absolute HTTPS URL for links in pages shared or saved separately')
    args=parser.parse_args()
    key=serialization.load_pem_public_key(args.public_key.read_bytes())
    result=publish(args.packages,key,args.output,args.base_url)
    print(f'Built static directory with {len(result["apps"])} apps: {args.output}')

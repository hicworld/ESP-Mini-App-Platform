"""Publish a staged OTA release to Tencent COS, then verify it from the public internet.

Uploading needs credentials; verifying does not, because the bucket is
public-read. `--verify-only` therefore runs anywhere and is how you confirm a
published object still matches the bytes on disk -- it is also the only part of
this script that can be exercised without a secret, so it is kept independent.

Credentials come from the environment only (TENCENTCLOUD_SECRET_ID /
TENCENTCLOUD_SECRET_KEY). They are never written to logs, to the verification
record, or to disk.

Uploads use the COS v5 signature over PUT Object, which is enough for images of
this size (a few megabytes) and avoids adding a COS SDK dependency.
"""
import argparse
import hashlib
import hmac
import json
import os
import sys
import time
import urllib.parse
from pathlib import Path

import requests

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUCKET = 'esp-mini-app-1315743965'
DEFAULT_REGION = 'ap-guangzhou'
SECRET_ID_ENV = 'TENCENTCLOUD_SECRET_ID'
SECRET_KEY_ENV = 'TENCENTCLOUD_SECRET_KEY'
UPLOAD_PARTS = (
    ('bin', 'application/octet-stream'),
    ('txt', 'text/plain'),
    ('zip', 'application/zip'),
)


def sha256_of(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cos_authorization(secret_id, secret_key, method, key, host, content_type, ttl=1800):
    """Build the q-sign-algorithm=sha1 Authorization header for COS."""
    start, end = int(time.time()) - 60, int(time.time()) + ttl
    key_time = f'{start};{end}'
    sign_key = hmac.new(secret_key.encode(), key_time.encode(), hashlib.sha1).hexdigest()

    signed = {'host': host}
    if content_type:
        signed['content-type'] = content_type
    names = sorted(signed)
    header_pairs = '&'.join(
        f'{urllib.parse.quote(n, safe="")}={urllib.parse.quote(signed[n], safe="")}' for n in names)

    pathname = '/' + key.lstrip('/')
    http_string = f'{method.lower()}\n{urllib.parse.quote(pathname, safe="/")}\n\n{header_pairs}\n'
    string_to_sign = f'sha1\n{key_time}\n{hashlib.sha1(http_string.encode()).hexdigest()}\n'
    signature = hmac.new(sign_key.encode(), string_to_sign.encode(), hashlib.sha1).hexdigest()

    return ('q-sign-algorithm=sha1'
            f'&q-ak={secret_id}'
            f'&q-sign-time={key_time}'
            f'&q-key-time={key_time}'
            f'&q-header-list={";".join(names)}'
            '&q-url-param-list='
            f'&q-signature={signature}')


def verify_public(url, expected_sha256, expected_size=None, timeout=120):
    """Download a published object and compare it byte for byte.

    Redirects are refused deliberately: the device expects a plain 200 with a
    definite Content-Length, so a redirect or a chunked response would be a
    publication defect even if the bytes happened to match.
    """
    response = requests.get(url, timeout=timeout, allow_redirects=False)
    record = {
        'url': url,
        'http_status': response.status_code,
        'redirect': response.headers.get('Location'),
        'content_type': response.headers.get('Content-Type'),
        'content_length_header': response.headers.get('Content-Length'),
        'expected_sha256': expected_sha256,
        'expected_size': expected_size,
    }
    if response.status_code != 200:
        record.update(ok=False, reason=f'HTTP {response.status_code}')
        return record
    if 'Location' in response.headers:
        record.update(ok=False, reason='server answered with a redirect')
        return record

    digest = hashlib.sha256(response.content).hexdigest()
    record.update(actual_sha256=digest, actual_size=len(response.content))
    if expected_size is not None and len(response.content) != expected_size:
        record.update(ok=False, reason=f'size {len(response.content)} != {expected_size}')
        return record
    if digest != expected_sha256:
        record.update(ok=False, reason='sha256 mismatch')
        return record
    if record['content_length_header'] is None:
        record.update(ok=False, reason='no Content-Length header')
        return record
    record.update(ok=True, reason='matches local bytes')
    return record


def release_files(version):
    """Locate the staged artefacts for a version."""
    ota = ROOT/'dist/ota'
    return {
        'bin': (ota/f'esp_mini_platform-{version}.bin', f'esp_mini_platform-{version}.bin'),
        'txt': (ota/f'SHA256SUMS-{version}.txt', f'SHA256SUMS-{version}.txt'),
        'zip': (ROOT/'dist'/f'ota-deploy-{version}.zip', f'ota-deploy-{version}.zip'),
    }


def load_plan(version):
    plan_path = ROOT/'dist/ota'/f'esp_mini_platform-{version}.bin.plan.json'
    if not plan_path.is_file():
        raise FileNotFoundError(f'no staged plan for {version}: {plan_path}')
    return json.loads(plan_path.read_text(encoding='utf-8'))


def publish(version, bucket, region, prefix, dry_run=False):
    plan = load_plan(version)
    host = f'{bucket}.cos.{region}.myqcloud.com'
    files = release_files(version)
    results = []

    for kind, content_type in UPLOAD_PARTS:
        local, name = files[kind]
        if not local.is_file():
            print(f'SKIP {name} (not staged)')
            continue
        key = f'{prefix.strip("/")}/{name}' if prefix else name
        url = f'https://{host}/{urllib.parse.quote(key, safe="/")}'
        digest = sha256_of(local)
        entry = {'file': name, 'key': key, 'url': url, 'size': local.stat().st_size, 'sha256': digest}

        if dry_run:
            entry.update(uploaded=False, note='dry run')
            results.append(entry)
            print(f'PLAN {name} -> {url} ({local.stat().st_size} bytes)')
            continue

        secret_id = os.environ.get(SECRET_ID_ENV)
        secret_key = os.environ.get(SECRET_KEY_ENV)
        if not secret_id or not secret_key:
            raise SystemExit(
                f'Missing credentials: set {SECRET_ID_ENV} and {SECRET_KEY_ENV} before uploading.\n'
                'Nothing was uploaded. `--verify-only` needs no credentials and still works.')
        auth = cos_authorization(secret_id, secret_key, 'put', key, host, content_type)
        response = requests.put(url, data=local.read_bytes(), timeout=300, headers={
            'Host': host,
            'Content-Type': content_type,
            'Authorization': auth,
        })
        if response.status_code not in (200, 201):
            raise SystemExit(f'Upload failed for {name}: HTTP {response.status_code}\n{response.text[:400]}')
        entry.update(uploaded=True, upload_status=response.status_code)
        entry['verification'] = verify_public(url, digest, local.stat().st_size)
        results.append(entry)
        print(f'UPLOAD {name} -> HTTP {response.status_code}, '
              f'verify: {entry["verification"]["reason"]}')

    record = {
        'version': version,
        'bucket': bucket,
        'region': region,
        'files': results,
        'published': not dry_run and all(f.get('uploaded') for f in results),
        'device_ota_verified': False,
    }
    if not dry_run:
        out = ROOT/'local'/f'cos-{version}-verification.json'
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(record, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
        print(f'Wrote {out.relative_to(ROOT)}')
    return record


def verify_only(version, url=None, expected=None):
    if url:
        if not expected:
            raise SystemExit('--url requires --sha256')
        return [verify_public(url, expected)]
    plan = load_plan(version)
    records = [verify_public(plan['proposed_url'], plan['sha256'], plan['size'])]
    for local, name in (release_files(version)['txt'], release_files(version)['zip']):
        if not local.is_file():
            continue
        base = plan['proposed_url'].rsplit('/', 1)[0]
        records.append(verify_public(f'{base}/{name}', sha256_of(local), local.stat().st_size))
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--version', default='0.4.4')
    parser.add_argument('--bucket', default=DEFAULT_BUCKET)
    parser.add_argument('--region', default=DEFAULT_REGION)
    parser.add_argument('--prefix', default='',
                        help='object key prefix; empty matches the historical root layout')
    parser.add_argument('--verify-only', action='store_true',
                        help='download and check published objects only (no credentials needed)')
    parser.add_argument('--url', help='explicit object URL to verify')
    parser.add_argument('--sha256', help='expected digest when using --url')
    parser.add_argument('--dry-run', action='store_true',
                        help='print the upload plan without contacting COS')
    parser.add_argument('--output', type=Path,
                        help='write verify-only results as JSON for release evidence')
    args = parser.parse_args()

    if args.verify_only or args.url:
        records = verify_only(args.version, args.url, args.sha256)
        for r in records:
            print(f'{"PASS" if r["ok"] else "FAIL"} {r["url"]}: {r["reason"]}')
        ok = all(r['ok'] for r in records)
        print(f'RESULT {sum(r["ok"] for r in records)}/{len(records)} verified')
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({
                'version': args.version,
                'files': records,
                'published': ok,
                'device_ota_verified': False,
            }, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
            print(f'Wrote {args.output}')
        return 0 if ok else 1

    record = publish(args.version, args.bucket, args.region, args.prefix, args.dry_run)
    if args.dry_run:
        print('Dry run: nothing uploaded.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

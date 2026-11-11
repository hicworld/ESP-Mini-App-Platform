"""Publish the board's latest-system pointer after its versioned image is verified."""
import argparse
import json
import os
from pathlib import Path

import requests

from publish_cos import (
    DEFAULT_BUCKET, DEFAULT_REGION, SECRET_ID_ENV, SECRET_KEY_ENV,
    cos_authorization, sha256_of, verify_public,
)

ROOT = Path(__file__).resolve().parents[1]
INDEX = ROOT / 'deploy/system/latest.json'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    index = json.loads(INDEX.read_text(encoding='utf-8'))
    version = index['version']
    name = f'esp_mini_platform-{version}.bin'
    if index != {'schema_version': 1, 'project': 'esp_mini_platform',
                 'version': version, 'file': name}:
        raise SystemExit('Invalid system index; expected the four release fields')
    image = ROOT / 'dist/ota' / name
    if not image.is_file():
        raise SystemExit(f'Missing staged image: {image}')
    host = f'{DEFAULT_BUCKET}.cos.{DEFAULT_REGION}.myqcloud.com'
    base = f'https://{host}/'
    url = base + 'system/latest.json'
    if args.dry_run:
        print(f'PLAN {INDEX} -> {url} ({INDEX.stat().st_size} bytes)')
        print(f'CHECK {base + name} against {image}')
        return
    result = verify_public(base + name, sha256_of(image), image.stat().st_size)
    if not result['ok']:
        raise SystemExit(f'Versioned image is not verified: {result["reason"]}')
    secret_id, secret_key = os.getenv(SECRET_ID_ENV), os.getenv(SECRET_KEY_ENV)
    if not secret_id or not secret_key:
        raise SystemExit('COS credentials are required to publish the system index')
    content_type = 'application/json'
    auth = cos_authorization(secret_id, secret_key, 'put', 'system/latest.json', host, content_type)
    response = requests.put(url, data=INDEX.read_bytes(), timeout=60, headers={
        'Host': host, 'Content-Type': content_type, 'Authorization': auth,
    })
    if response.status_code not in (200, 201):
        raise SystemExit(f'Index upload failed: HTTP {response.status_code}')
    result = verify_public(url, sha256_of(INDEX), INDEX.stat().st_size)
    if not result['ok']:
        raise SystemExit(f'Public index verification failed: {result["reason"]}')
    print(f'Published and verified {url} -> {name}')


if __name__ == '__main__':
    main()

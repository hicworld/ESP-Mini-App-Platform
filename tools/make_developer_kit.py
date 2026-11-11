"""Package the public app SDK and examples; never include signing secrets."""
import argparse
import hashlib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = (
    '启动本地工作台.cmd',
    'requirements-app-sdk.txt',
    'tools/setup_sdk.ps1',
    'tools/image_to_c.py',
    'tools/build_c_app.py',
    'tools/build_rust_app.py',
    'tools/check_submission.py',
    'tools/review_submission.py',
    'tools/node_runtime.py',
    'tools/developer_studio.py',
    'tools/developer_studio.html',
    'tools/developer_studio.js',
    'tools/studio.css',
    'tools/studio_home.js',
    'tools/studio_project.html',
    'tools/studio_project.js',
    'tools/studio_preview.html',
    'tools/studio_preview.js',
    'tools/studio_app.html',
    'tools/studio_app.js',
    'tools/studio_examples.html',
    'tools/studio_examples.js',
    'tools/studio_docs.html',
    'tools/studio_docs.js',
    'tools/studio_review.html',
    'tools/studio_review.js',
    'tools/app_package.py',
    'tools/abi_check.mjs',
    'sdk/include/platform.h',
    'sdk/include/capabilities.h',
    'sdk/include/scene.h',
    'sdk/rust/miniapp/Cargo.toml',
    'sdk/rust/miniapp/src/lib.rs',
    'docs/SDK.md',
    'docs/PLATFORM_API.md',
    'docs/FIRST_BATCH_CAPABILITIES.md',
    'docs/THIRD_PARTY_CODE_GUIDE.md',
    'docs/app_format.md',
    'docs/THIRD_PARTY_APPS.md',
    'docs/APP_ID_REGISTRY.md',
    'docs/STORE_DEPLOYMENT.md',
    'docs/HARDWARE_REQUIREMENTS.md',
    'templates/third_party_app/main.c',
    'templates/third_party_app/manifest.json',
    'templates/third_party_app/投稿信息.md',
    'examples/README.md',
    'examples/c_first_batch/main.c',
    'examples/c_first_batch/manifest.json',
    'examples/c_first_batch/artwork.h',
    'examples/c_first_batch/artwork.png',
    'examples/c_color_pad/main.c',
    'examples/c_color_pad/manifest.json',
    'examples/c_scene_timer/main.c',
    'examples/c_scene_timer/manifest.json',
    'examples/c_optional_input/main.c',
    'examples/c_optional_input/manifest.json',
    'examples/rust_saved_counter/Cargo.toml',
    'examples/rust_saved_counter/Cargo.lock',
    'examples/rust_saved_counter/manifest.json',
    'examples/rust_saved_counter/src/lib.rs',
    'apps/snake/main.c',
    'apps/snake/manifest.json',
    'apps/bounce/main.cpp',
    'apps/bounce/manifest.json',
    'apps/rust_bounce/Cargo.toml',
    'apps/rust_bounce/Cargo.lock',
    'apps/rust_bounce/manifest.json',
    'apps/rust_bounce/src/lib.rs',
)


def make(output):
    output = output.resolve()
    if output in {(ROOT/path).resolve() for path in FILES}:
        raise ValueError('output would overwrite an SDK source file')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(ROOT/'developer_kit/README.md', 'README.md')
        for relative in FILES:
            archive.write(ROOT/relative, relative)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    checksum = output.with_name(output.name + '.sha256')
    checksum.write_text(f'{digest}  {output.name}\n', encoding='ascii')
    print(f'Developer kit: {output} ({len(FILES) + 1} files, no private key)')
    print(f'SHA256: {digest} ({checksum})')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'dist/ESP-Mini-App-Developer-Kit.zip')
    args = parser.parse_args()
    make(args.output)

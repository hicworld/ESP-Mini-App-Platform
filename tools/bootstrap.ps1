param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if (!(Test-Path external/wamr/build-scripts/runtime_lib.cmake)) {
        git clone --depth 1 --branch WAMR-2.4.5 https://github.com/bytecodealliance/wasm-micro-runtime.git external/wamr
        if ($LASTEXITCODE) { throw 'WAMR download failed' }
    }
    $revision = git -C external/wamr rev-parse HEAD
    if ($revision -ne '25bd7eb63e828e4bd242cc9b38d260b4b31c6605') { throw 'Unexpected WAMR revision' }
    Push-Location tools
    try {
        if (!(Test-Path node_modules/wabt)) {
            if (Get-Command npm -ErrorAction SilentlyContinue) { npm install --ignore-scripts }
            else {
                $nodeDir = Split-Path (Split-Path (Get-Command node).Source -Parent) -Parent
                node (Join-Path $nodeDir 'node_modules/pnpm/bin/pnpm.cjs') install --ignore-scripts
            }
            if ($LASTEXITCODE) { throw 'Package install failed' }
        }
        node build_apps.mjs; if ($LASTEXITCODE) { throw 'WAT build failed' }
    } finally { Pop-Location }
    if (!(Test-Path local/dev_private.pem) -and !(Test-Path firmware/main/trusted_key.pem)) {
        & $Python tools/app_package.py keygen --private local/dev_private.pem --public firmware/main/trusted_key.pem
        if ($LASTEXITCODE) { throw 'Key generation failed (install requirements-dev.txt)' }
    }
    foreach ($name in @('hello','touch')) {
        & $Python tools/app_package.py pack "apps/$name/manifest.json" "dist/$name.wasm" "dist/$name.app" --key local/dev_private.pem
        if ($LASTEXITCODE) { throw 'Signing failed; keep your original local key' }
    }
    Copy-Item -LiteralPath dist/hello.app -Destination firmware/main/builtin.app
    Copy-Item -LiteralPath dist/touch.app -Destination firmware/main/touch.app
    & $Python tools/build_c_app.py apps/snake
    if ($LASTEXITCODE) { throw 'C SDK build failed; run tools/setup_sdk.ps1 first' }
    Copy-Item -LiteralPath dist/snake.app -Destination firmware/main/snake.app
    & $Python tools/build_store.py dist/hello.app dist/touch.app dist/snake.app
    if ($LASTEXITCODE) { throw 'Static app directory generation failed' }
    node tools/build_fixtures.mjs
    if ($LASTEXITCODE) { throw 'Fixture compilation failed' }
    & $Python tools/package_fixtures.py
    if ($LASTEXITCODE) { throw 'Fixture packaging failed' }
} finally { Pop-Location }

param(
    [ValidateSet('Build','Check','Menuconfig','Merge','Flash','Monitor','Test')][string]$Action='Build',
    [string]$ProjectVersion='0.4.11',
    [string]$Port,
    [string]$BuildRoot='C:\esp\mini-app-mosaico-build',
    [string]$Activate='C:\Espressif\tools\Microsoft.release-v6.1.PowerShell_profile.ps1'
)
$ErrorActionPreference='Stop'
if ($Action -eq 'Test') {
    Push-Location $PSScriptRoot
    try {
        python -m pytest tests -q; if ($LASTEXITCODE) { throw 'Tests failed' }
        node tools/runtime_test.mjs; if ($LASTEXITCODE) { throw 'Runtime tests failed' }
        node tools/snake_test.mjs; if ($LASTEXITCODE) { throw 'Snake tests failed' }
        python tools/build_c_app.py apps/garden; if ($LASTEXITCODE) { throw 'Garden build failed' }
        node tools/garden_test.mjs; if ($LASTEXITCODE) { throw 'Garden tests failed' }
        python tools/build_c_app.py apps/bounce; if ($LASTEXITCODE) { throw 'C++ bounce build failed' }
        node tools/bounce_test.mjs dist/bounce.wasm; if ($LASTEXITCODE) { throw 'C++ bounce tests failed' }
        # Rust is not part of the project bootstrap; build it only when present.
        # ~/.cargo/bin is often absent from PATH even when rustup is installed.
        $hasCargo = (Get-Command cargo -ErrorAction SilentlyContinue) -or (Test-Path (Join-Path $HOME '.cargo\bin\cargo.exe'))
        if ($hasCargo) {
            python tools/build_rust_app.py apps/rust_bounce; if ($LASTEXITCODE) { throw 'Rust bounce build failed' }
            node tools/bounce_test.mjs dist/rust_bounce.wasm; if ($LASTEXITCODE) { throw 'Rust bounce tests failed' }
        } else {
            Write-Host 'Rust toolchain absent; skipping apps/rust_bounce (see docs/SDK.md)'
        }
        python tools/test_scene_host.py; if ($LASTEXITCODE) { throw 'Scene renderer tests failed' }
        python tools/test_network_ui.py; if ($LASTEXITCODE) { throw 'Network UI tests failed' }
        python tools/test_audio_host.py; if ($LASTEXITCODE) { throw 'Audio synthesis tests failed' }
        python tools/test_store_host.py; if ($LASTEXITCODE) { throw 'Firmware store parser tests failed' }
        python tools/test_system_update_host.py; if ($LASTEXITCODE) { throw 'System update manifest tests failed' }
        python tools/test_storage_host.py; if ($LASTEXITCODE) { throw 'Firmware storage fault tests failed' }
        python tools/test_runtime_host.py; if ($LASTEXITCODE) { throw 'Runtime host boundary tests failed' }
        python tools/test_app_data_host.py; if ($LASTEXITCODE) { throw 'Application data tests failed' }
        python tools/test_ota_health_host.py; if ($LASTEXITCODE) { throw 'OTA health gate tests failed' }
        python tools/test_abi.py; if ($LASTEXITCODE) { throw 'ABI v1 load policy checks failed' }
    } finally { Pop-Location }
    exit
}
if (($Action -eq 'Flash' -or $Action -eq 'Monitor') -and !$Port) { throw 'Specify -Port COM19 (confirm your device first)' }
if (!(Test-Path -LiteralPath $Activate)) { throw 'Set -Activate to your ESP-IDF 6.1 environment script' }
$env:PYTHONUTF8='1'
$env:CMAKE_BUILD_PARALLEL_LEVEL='4'
. $Activate
$snapshot = python (Join-Path $PSScriptRoot 'tools/prepare_build.py') $PSScriptRoot $BuildRoot
if ($LASTEXITCODE) { throw 'Could not prepare ASCII build cache' }
$buildDir=Join-Path $BuildRoot 'build'
$cmakeArgs=@("-DPLATFORM_PROJECT_VERSION=$ProjectVersion")
Push-Location (Join-Path $snapshot 'firmware')
try {
    if (!(Test-Path sdkconfig)) { idf.py --preview -B $buildDir -DIDF_TARGET=esp32s31 @cmakeArgs reconfigure; if ($LASTEXITCODE) { throw 'Target setup failed' } }
    switch ($Action) {
        Check {
            if (!(Test-Path (Join-Path $buildDir 'build.ninja'))) {
                idf.py --preview -B $buildDir @cmakeArgs reconfigure
                if ($LASTEXITCODE) { throw 'Configure failed' }
            }
            $objectTargets=@(Get-Content (Join-Path $buildDir 'build.ninja') | ForEach-Object {
                if ($_ -match '^build (esp-idf/(?:main|platform_runtime)/[^:]+\.obj):') { $Matches[1] }
            })
            if (!$objectTargets.Count) { throw 'No application/runtime object targets found' }
            ninja -C $buildDir -j 4 @objectTargets
        }
        Build {
            idf.py --preview -B $buildDir @cmakeArgs build
            if ($LASTEXITCODE) { throw 'Build failed' }
            idf.py --preview -B $buildDir merge-bin
        }
        Menuconfig { idf.py --preview -B $buildDir @cmakeArgs menuconfig }
        Merge { idf.py --preview -B $buildDir @cmakeArgs merge-bin }
        Flash {
            idf.py --preview -B $buildDir @cmakeArgs build
            if ($LASTEXITCODE) { throw 'Build before flash failed' }
            Push-Location $buildDir
            try {
                # ROM USB full writes avoid the unreliable differential path on this board.
                # This board's USB-OTG CDC link is reliable at 115200; higher
                # rates can disconnect during the large application write.
                python -m esptool --chip esp32s31 -p $Port -b 115200 --before default-reset --after hard-reset --no-stub write-flash '@flash_args'
            } finally { Pop-Location }
        }
        Monitor { idf.py --preview -B $buildDir -p $Port monitor }
    }
    if ($LASTEXITCODE) { throw "idf.py $Action failed" }
    Copy-Item -LiteralPath 'sdkconfig' -Destination (Join-Path $PSScriptRoot 'firmware/sdkconfig')
    if ($Action -eq 'Build' -or $Action -eq 'Merge') {
        $output=Join-Path $PSScriptRoot 'dist/firmware'
        New-Item -ItemType Directory -Path $output -Force | Out-Null
        $checksums=@()
        foreach ($file in @('esp_mini_platform.bin','esp_mini_platform.elf','esp_mini_platform.map','flasher_args.json','merged-binary.bin','bootloader/bootloader.bin','partition_table/partition-table.bin','ota_data_initial.bin')) {
            $artifact=Join-Path $buildDir $file
            if (Test-Path -LiteralPath $artifact) {
                $destination=Join-Path $output $file
                New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
                Copy-Item -LiteralPath $artifact -Destination $destination
                if ($file.EndsWith('.bin')) {
                    $hash=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
                    $checksums+="$hash  $file"
                }
            }
        }
        $checksums | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
    }
} finally { Pop-Location }

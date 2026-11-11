param([string]$Destination='C:\esp\mini-app-sdk')
$ErrorActionPreference='Stop'
$sdkName='wasi-sdk-34.0-x86_64-windows'
$digest='cccb5c323a9b34f0349a9b09e8804a0a7632c68c3310f4b5f437ed57d7e71d8f'
$archive=Join-Path $Destination 'wasi-sdk-34.tar.gz'
New-Item -ItemType Directory -Force $Destination | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    curl.exe -L --fail --retry 2 --output $archive "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-34/$sdkName.tar.gz"
    if ($LASTEXITCODE) { throw 'SDK download failed' }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $digest) { throw 'SDK archive checksum mismatch' }
if (!(Test-Path -LiteralPath (Join-Path $Destination "$sdkName/bin/clang.exe"))) {
    tar -xzf $archive -C $Destination
    if ($LASTEXITCODE) { throw 'SDK extraction failed' }
}
Write-Output "SDK ready: $(Join-Path $Destination $sdkName)"

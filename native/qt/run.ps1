$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
& (Join-Path $repo 'native/windows/build-media.ps1')
& (Join-Path $PSScriptRoot 'build.ps1')
if($LASTEXITCODE -ne 0){throw 'Qt development build failed'}
Start-Process -FilePath (Join-Path $repo 'release/qt-unpacked/GoLive P2P.exe') -WindowStyle Hidden

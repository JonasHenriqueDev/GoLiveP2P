$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$format=Join-Path $repo 'work/qt-tools/Scripts/clang-format.exe'
if(!(Test-Path $format)){throw 'Run npm run setup to install clang-format'}
$sources=Get-ChildItem $PSScriptRoot -File | Where-Object Extension -in '.cpp','.hpp' | Select-Object -ExpandProperty FullName
& $format --dry-run -Werror @sources
if($LASTEXITCODE -ne 0){throw 'C++ formatting check failed'}
node --check (Join-Path $PSScriptRoot 'smoke.mjs')
node --check (Join-Path $PSScriptRoot 'verify-package.mjs')
if($LASTEXITCODE -ne 0){throw 'Development check syntax failed'}
Write-Output 'Qt C++ formatting and development checks passed.'

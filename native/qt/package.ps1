$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$nsis=Get-Command makensis.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
if(!$nsis){$nsis=Get-ChildItem "$env:LOCALAPPDATA/electron-builder/Cache" -Recurse -Filter makensis.exe -ErrorAction SilentlyContinue | Where-Object { $_.Directory.Name -ne 'Bin' } | Select-Object -ExpandProperty FullName -First 1}
if(!$nsis){throw 'Install NSIS 3 and add makensis.exe to PATH.'}
$stage=Join-Path $repo 'release/qt-unpacked'
$manifest=Get-ChildItem $stage -File -Recurse | Where-Object Name -ne 'package-manifest.json' | ForEach-Object {@{path=[IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()}}
$manifest | ConvertTo-Json -Depth 4 | Set-Content "$stage/package-manifest.json" -Encoding utf8
Push-Location $PSScriptRoot
try{& $nsis /V2 installer.nsi;if($LASTEXITCODE -ne 0){throw 'NSIS build failed'}}finally{Pop-Location}
Get-FileHash (Join-Path $repo 'release/GoLive-P2P-Setup-0.6.0-qt.3.exe') -Algorithm SHA256

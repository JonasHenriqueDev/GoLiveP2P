param([string]$QtRoot,[switch]$Package)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(!$QtRoot){$QtRoot=Join-Path $repo 'work/qt-sdk/6.8.3/msvc2022_64'}
$QtRoot=(Resolve-Path $QtRoot).Path
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$build=Join-Path $repo 'work/qt-build'
$tools=Join-Path $repo 'work/qt-tools/Scripts'
New-Item -ItemType Directory -Force $build | Out-Null
$batch=Join-Path $build 'build.cmd'
$lines=@('@echo off',"call `"$vs/VC/Auxiliary/Build/vcvars64.bat`" >nul", "`"$tools/cmake.exe`" -S `"$PSScriptRoot`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=`"$QtRoot`" -DCMAKE_MAKE_PROGRAM=`"$tools/ninja.exe`"",'if errorlevel 1 exit /b 1',"`"$tools/cmake.exe`" --build `"$build`"",'if errorlevel 1 exit /b 1',"set PATH=$QtRoot/bin;%PATH%", "`"$tools/ctest.exe`" --test-dir `"$build`" --output-on-failure",'exit /b %errorlevel%')
[IO.File]::WriteAllLines($batch,$lines,[Text.Encoding]::ASCII)
& cmd.exe /d /c "`"$batch`""
if($LASTEXITCODE -ne 0){throw 'Qt build/tests failed'}
$stage=Join-Path $repo 'release/qt-unpacked'
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item -LiteralPath "$build/GoLive P2P.exe" -Destination $stage -Force
& "$QtRoot/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --dir $stage "$stage/GoLive P2P.exe"
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
$crt=Get-ChildItem "$vs/VC/Redist/MSVC" -Directory | Sort-Object Name -Descending | ForEach-Object {Get-ChildItem "$($_.FullName)/x64" -Directory -Filter '*.CRT' -ErrorAction SilentlyContinue} | Select-Object -First 1
if(!$crt){throw 'Redistributable MSVC x64 DLLs were not found'}
Get-ChildItem $crt.FullName -Filter '*.dll' | Copy-Item -Destination $stage -Force
New-Item -ItemType Directory -Force "$stage/licenses" | Out-Null
Get-ChildItem "$PSScriptRoot/licenses" | Copy-Item -Destination "$stage/licenses" -Recurse -Force
New-Item -ItemType Directory -Force (Join-Path $stage 'native-media') | Out-Null
Get-ChildItem (Join-Path $repo 'native/windows/runtime') | Copy-Item -Destination (Join-Path $stage 'native-media') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD-PARTY.md') -Destination $stage -Force
if($Package){& (Join-Path $PSScriptRoot 'package.ps1');if($LASTEXITCODE -ne 0){throw 'Qt installer failed'}}
Write-Output "Qt application deployed: $stage"

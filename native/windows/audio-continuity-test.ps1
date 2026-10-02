$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$build=Join-Path $repo 'work/native-build'
$commands=@('@echo off',"call `"$vs/VC/Auxiliary/Build/vcvars64.bat`" >nul","cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Fo`"$build/audio-continuity-test.obj`" /Fe`"$build/audio-continuity-test.exe`" `"$PSScriptRoot/audio-continuity-test.cpp`"",'exit /b %errorlevel%')
[IO.File]::WriteAllLines("$build/audio-continuity-test.cmd",$commands,[Text.Encoding]::ASCII)
& cmd.exe /d /c "`"$build/audio-continuity-test.cmd`""
if($LASTEXITCODE -ne 0){throw 'Audio continuity test compilation failed'}
& "$build/audio-continuity-test.exe"
if($LASTEXITCODE -ne 0){throw 'Audio continuity regression'}

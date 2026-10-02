$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC C++ build tools were not found.' }
$output = Join-Path $repo 'work/native-audio-tests'
New-Item -ItemType Directory -Force $output | Out-Null
$batch = Join-Path $output 'compile.cmd'
$commands = @(
  '@echo off',
  "call `"$vs/VC/Auxiliary/Build/vcvars64.bat`" >nul",
  "cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Fo`"$output/process-audio-test.obj`" /Fe`"$output/process-audio-test.exe`" `"$PSScriptRoot/process-audio-test.cpp`" /link ole32.lib mmdevapi.lib user32.lib",
  'exit /b %errorlevel%'
)
[IO.File]::WriteAllLines($batch, $commands, [Text.Encoding]::ASCII)
& cmd.exe /d /c "`"$batch`""
if ($LASTEXITCODE -ne 0) { throw 'Native audio tests did not compile.' }
& "$output/process-audio-test.exe"
if ($LASTEXITCODE -ne 0) { throw 'Native audio tests failed.' }

param([string]$Sdk = $env:GOLIVE_GSTREAMER_ROOT)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if(!$Sdk){$Sdk=(Get-Content "$PSScriptRoot/sdk-root.txt" -Raw).Trim()}
$Sdk=(Resolve-Path $Sdk).Path
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC required'}
$build=Join-Path $repo 'work/native-build'
New-Item -ItemType Directory -Force $build | Out-Null
$includes=@("$Sdk/include/gstreamer-1.0","$Sdk/include/glib-2.0","$Sdk/lib/glib-2.0/include") | ForEach-Object {'/I"'+$_+'"'}
$lines=@('@echo off',"call `"$vs/VC/Auxiliary/Build/vcvars64.bat`" >nul", "cl /nologo /std:c++17 /EHsc /W4 /O2 /MT $($includes -join ' ') /Fo`"$build/receive-buffer-test.obj`" /Fe`"$build/receive-buffer-test.exe`" `"$PSScriptRoot/receive-buffer-test.cpp`" /link /LIBPATH:`"$Sdk/lib`" gstreamer-1.0.lib gstapp-1.0.lib gobject-2.0.lib glib-2.0.lib",'exit /b %errorlevel%')
[IO.File]::WriteAllLines("$build/receive-buffer-test.cmd",$lines,[Text.Encoding]::ASCII)
& cmd.exe /d /c "`"$build/receive-buffer-test.cmd`""
if($LASTEXITCODE -ne 0){throw 'Receive buffer test compilation failed'}
$env:PATH="$Sdk/bin;"+$env:PATH
$env:GST_PLUGIN_PATH="$Sdk/lib/gstreamer-1.0"
$env:GST_PLUGIN_SYSTEM_PATH=$env:GST_PLUGIN_PATH
& "$build/receive-buffer-test.exe"
if($LASTEXITCODE -ne 0){throw 'RTP receive buffer regression'}

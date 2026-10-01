param([string]$Sdk = $env:GOLIVE_GSTREAMER_ROOT)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if(!$Sdk -and (Test-Path "$PSScriptRoot/sdk-root.txt")){$Sdk=(Get-Content "$PSScriptRoot/sdk-root.txt" -Raw).Trim()}
if(!$Sdk){$Sdk=Join-Path $PSScriptRoot 'sdk/PFiles64/gstreamer/1.0/msvc_x86_64'}
if (!$Sdk -or !(Test-Path "$Sdk/include/gstreamer-1.0/gst/gst.h")) { throw 'Use npm run native:setup or set GOLIVE_GSTREAMER_ROOT to the verified GStreamer 1.26.7 MSVC x64 SDK.' }
$Sdk = (Resolve-Path $Sdk).Path
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'MSVC C++ build tools were not found.' }
$stage = Join-Path $PSScriptRoot 'runtime'
New-Item -ItemType Directory -Force "$stage/bin", "$stage/lib/gstreamer-1.0", "$stage/libexec/gstreamer-1.0", "$stage/licenses", "$repo/work/native-build" | Out-Null
$batch = Join-Path $repo 'work/native-build/compile.cmd'
$includes = @("$Sdk/include/gstreamer-1.0", "$Sdk/include/glib-2.0", "$Sdk/lib/glib-2.0/include") | ForEach-Object { '/I"' + $_ + '"' }
$libraries = 'gstreamer-1.0.lib gstapp-1.0.lib gstwebrtc-1.0.lib gstsdp-1.0.lib gstvideo-1.0.lib gobject-2.0.lib glib-2.0.lib dwmapi.lib user32.lib ole32.lib mmdevapi.lib'
$commands = @(
  '@echo off',
  "call `"$vs/VC/Auxiliary/Build/vcvars64.bat`" >nul",
  "cl /nologo /std:c++17 /EHsc /W4 /O2 /MT $($includes -join ' ') /Fo`"$repo/work/native-build/media-engine.obj`" /Fe`"$stage/media-engine.exe`" `"$PSScriptRoot/media-engine.cpp`" /link /LIBPATH:`"$Sdk/lib`" $libraries",
  'if errorlevel 1 exit /b 1',
  "cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /Fo`"$repo/work/native-build/audio-capture.obj`" /Fe`"$PSScriptRoot/bin/audio-capture.exe`" `"$PSScriptRoot/audio-capture.cpp`" /link ole32.lib mmdevapi.lib user32.lib",
  'if errorlevel 1 exit /b 1',
  "cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /Fo`"$repo/work/native-build/media-fixture.obj`" /Fe`"$repo/work/native-build/media-fixture.exe`" `"$PSScriptRoot/media-fixture.cpp`" /link winmm.lib user32.lib gdi32.lib",
  'exit /b %errorlevel%'
)
[IO.File]::WriteAllLines($batch, $commands, [Text.Encoding]::ASCII)
& cmd.exe /d /c "`"$batch`""
if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed' }
Copy-Item "$Sdk/bin/*.dll" "$stage/bin" -Force
Copy-Item "$Sdk/lib/gstreamer-1.0/*.dll" "$stage/lib/gstreamer-1.0" -Force
Copy-Item "$Sdk/libexec/gstreamer-1.0/gst-plugin-scanner.exe" "$stage/libexec/gstreamer-1.0" -Force
Copy-Item "$PSScriptRoot/vendor/LICENSE-json.txt" "$stage/licenses" -Force
if (Test-Path "$Sdk/share/licenses") { New-Item -ItemType Directory -Force "$stage/licenses/sdk" | Out-Null; Get-ChildItem "$Sdk/share/licenses" | Copy-Item -Destination "$stage/licenses/sdk" -Recurse -Force }
Copy-Item "$PSScriptRoot/THIRD-PARTY.md" "$stage/licenses" -Force
$manifest = Get-ChildItem $stage -File -Recurse | Where-Object Name -ne 'manifest.json' | ForEach-Object { @{ path = [IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/'); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(); bytes = $_.Length } }
$manifest | ConvertTo-Json -Depth 4 | Set-Content "$stage/manifest.json" -Encoding utf8
Write-Output "Native runtime staged at $stage"

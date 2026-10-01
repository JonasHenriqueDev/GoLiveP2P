param([ValidateSet('game','bitblt','wgc')][string]$Mode='game', [switch]$Chrome)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path "$PSScriptRoot/../..").Path
$work=Join-Path $repo 'work/obs-evaluation'
New-Item -ItemType Directory -Force $work | Out-Null
function VerifiedDownload($name,$hash) {
  $path=Join-Path $work $name
  if(!(Test-Path $path)) {
    & curl.exe -L --fail --retry 2 --max-time 180 -o $path "https://github.com/obsproject/obs-studio/releases/download/32.2.2/$name"
    if($LASTEXITCODE -ne 0){throw "OBS download failed: $name"}
  }
  if((Get-FileHash $path -Algorithm SHA256).Hash -ne $hash){throw "OBS digest mismatch: $name"}
  return $path
}
$zip=VerifiedDownload 'OBS-Studio-32.2.2-Windows-x64.zip' '4d6e40e3ab155f56b30de517380566a206d74b63cdf5ad49aa596924768f97e1'
$source=VerifiedDownload 'OBS-Studio-32.2.2-Sources.tar.gz' 'ec81fb66b03e75ddb3076b576f62679c39262e0e9960cef3e17a40dc5d68e6b4'
$runtime=Join-Path $work 'runtime'
if(!(Test-Path "$runtime/bin/64bit/obs.dll")){Expand-Archive -LiteralPath $zip -DestinationPath $runtime}
& tar.exe -xf $source -C $work 'obs-studio-32.2.2-sources/libobs'
if($LASTEXITCODE -ne 0){throw 'OBS header extraction failed'}
$headers=Join-Path $work 'obs-studio-32.2.2-sources/libobs'
@'
#pragma once
#define OBS_DATA_PATH "data"
#define OBS_PLUGIN_PATH "obs-plugins/64bit"
#define OBS_PLUGIN_DESTINATION "obs-plugins"
#define OBS_INSTALL_PREFIX "."
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
'@ | Set-Content "$headers/obsconfig.h" -Encoding ascii
$vs=& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'MSVC not found'}
@('@echo off', "call `"$vs/VC/Auxiliary/Build/vcvars64.bat`" >nul", "cl /nologo /std:c++17 /EHsc /W4 /O2 /MT /I`"$headers`" /Fe`"$work/probe.exe`" /Fo`"$work/probe.obj`" `"$PSScriptRoot/obs-probe.cpp`" /link user32.lib", 'exit /b %errorlevel%') | Set-Content "$work/compile.cmd" -Encoding ascii
& cmd.exe /d /c "`"$work/compile.cmd`""
if($LASTEXITCODE -ne 0){throw 'OBS probe compile failed'}
# OBS's win-capture plugin can update its hooks in ProgramData and Vulkan registry.
# This evaluation is separate from production packaging; it is not run by build:win.
Push-Location $repo
$fixture=$null
try {
  if(!$Chrome){$fixture=Start-Process -FilePath "$repo/work/native-build/media-fixture.exe" -WindowStyle Hidden -PassThru}
  $arguments=@($runtime,$Mode)
  if($Chrome){$arguments+='chrome'}
  & "$work/probe.exe" @arguments *> "$work/$Mode-evaluation.log"
  $probeExit=$LASTEXITCODE
  Get-Content "$work/$Mode-evaluation.log" -Tail 20
  if($probeExit -ne 0){throw "OBS evaluation failed: $probeExit"}
} finally {
  if($fixture -and !$fixture.HasExited){Stop-Process -Id $fixture.Id}
  Pop-Location
}

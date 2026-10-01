$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$cache=Join-Path $repo 'work/sdk'
New-Item -ItemType Directory -Force $cache | Out-Null
$base='https://gstreamer.freedesktop.org/data/pkg/windows/1.26.7/msvc'
$packages=@(
  @{name='gstreamer-1.0-msvc-x86_64-1.26.7.msi';hash='169d94f6f9c817830124946fff02deffe2f5cbf6fe48895339027f023c1838d0'},
  @{name='gstreamer-1.0-devel-msvc-x86_64-1.26.7.msi';hash='9d8b4d60f4ba8457de704d9b444695f4a7b2f1490011c39c2708aaf96c608c30'}
)
foreach($package in $packages){
  $path=Join-Path $cache $package.name
  if(!(Test-Path $path)){
    & curl.exe -sS -L --fail --retry 2 --connect-timeout 20 --max-time 600 -o $path "$base/$($package.name)"
    if($LASTEXITCODE -ne 0){throw "Download failed: $($package.name)"}
  }
  if((Get-FileHash $path -Algorithm SHA256).Hash.ToLower() -ne $package.hash){throw "SDK SHA-256 mismatch: $path"}
  $target=Join-Path $PSScriptRoot 'sdk'
  $extract=Start-Process msiexec.exe -WindowStyle Hidden -ArgumentList @('/a',('"'+$path+'"'),'/qn',('TARGETDIR="'+$target+'"')) -Wait -PassThru
  if($extract.ExitCode -ne 0){throw "SDK extraction failed: $($extract.ExitCode)"}
}
$sdk=Join-Path $PSScriptRoot 'sdk/PFiles64/gstreamer/1.0/msvc_x86_64'
& "$PSScriptRoot/build-media.ps1" -Sdk $sdk

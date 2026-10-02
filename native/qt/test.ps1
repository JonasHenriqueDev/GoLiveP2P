$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
& (Join-Path $PSScriptRoot 'build.ps1')
& (Join-Path $repo 'native/windows/process-audio-test.ps1')
node (Join-Path $repo 'native/windows/policy-test.mjs')
if($LASTEXITCODE -ne 0){throw 'Native policy checks failed'}

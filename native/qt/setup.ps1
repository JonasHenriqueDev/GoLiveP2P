$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $repo
try {
  if(!(Test-Path 'work/qt-tools/Scripts/python.exe')){py -3.13 -m venv work/qt-tools;if($LASTEXITCODE -ne 0){throw 'Python 3.13 is required for SDK setup'}}
  & work/qt-tools/Scripts/python.exe -m pip install aqtinstall==3.3.0 cmake==4.4.3 ninja==1.13.2 clang-format==23.1.2
  if($LASTEXITCODE -ne 0){throw 'Build tools setup failed'}
  & work/qt-tools/Scripts/python.exe -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir work/qt-sdk --archives qtbase qttools --modules qtwebsockets
  if($LASTEXITCODE -ne 0){throw 'Qt SDK setup failed'}
} finally {Pop-Location}

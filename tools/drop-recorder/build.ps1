param([string]$BuildDirectory='build',[string]$QtRoot="$env:USERPROFILE/Documents/Qt")
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../build-environment.ps1"
& $BuildTools.CMake -S $PSScriptRoot -B "$PSScriptRoot/$BuildDirectory" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_MAKE_PROGRAM=$($BuildTools.Ninja)" "-DCMAKE_RC_COMPILER=$($BuildTools.RC)" "-DCMAKE_MT=$($BuildTools.MT)"
if($LASTEXITCODE){throw 'Configure failed'}
& $BuildTools.CMake --build "$PSScriptRoot/$BuildDirectory" --parallel 2
if($LASTEXITCODE){throw 'Build failed'}

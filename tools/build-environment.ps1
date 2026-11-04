param([string]$VsInstallPath, [string]$WindowsSdkRoot)
$ErrorActionPreference='Stop'
if (!$VsInstallPath) {
  $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $locator) { $VsInstallPath = & $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
}
# Also support a relocated toolchain whose Windows registration is stale.
if (!$VsInstallPath) {
  foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
    $base=Join-Path $drive.Root 'Program Files (x86)\Microsoft Visual Studio'
    if (Test-Path $base) {
      $candidate=Get-ChildItem "$base\*\*\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
      if ($candidate) { $VsInstallPath=Split-Path (Split-Path (Split-Path $candidate.FullName)); break }
    }
  }
}
if (!$VsInstallPath -or !(Test-Path "$VsInstallPath\VC\Tools\MSVC")) { throw 'MSVC not found. Supply -VsInstallPath with a Visual Studio C++ installation.' }
$vc=Get-ChildItem "$VsInstallPath\VC\Tools\MSVC" -Directory | Sort-Object { [version]$_.Name } -Descending | Where-Object { Test-Path "$($_.FullName)\bin\Hostx64\x64\cl.exe" } | Select-Object -First 1
if (!$WindowsSdkRoot) {
  $WindowsSdkRoot=(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
  if (!$WindowsSdkRoot -or !(Test-Path $WindowsSdkRoot)) {
    foreach ($drive in Get-PSDrive -PSProvider FileSystem) {
      $candidate=Join-Path $drive.Root 'Program Files (x86)\Windows Kits\10'
      if (Test-Path $candidate) { $WindowsSdkRoot=$candidate; break }
    }
  }
}
if (!$WindowsSdkRoot) { throw 'Windows SDK not found. Supply -WindowsSdkRoot.' }
$sdk=Get-ChildItem "$WindowsSdkRoot\Include" -Directory | Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending | Where-Object {
  (Test-Path "$WindowsSdkRoot\bin\$($_.Name)\x64\rc.exe") -and (Test-Path "$WindowsSdkRoot\bin\$($_.Name)\x64\mt.exe") -and
  (Test-Path "$($_.FullName)\winrt\wrl.h") -and (Test-Path "$WindowsSdkRoot\Lib\$($_.Name)\um\x64\kernel32.lib") -and
  (Test-Path "$WindowsSdkRoot\Lib\$($_.Name)\ucrt\x64\ucrt.lib")
} | Select-Object -First 1
if (!$vc -or !$sdk) { throw 'Incomplete MSVC/SDK: x64 compiler, UCRT, WinRT headers, rc and mt are required.' }
$cmakeBase=Join-Path $VsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake=Join-Path $cmakeBase 'CMake\bin\cmake.exe'
$ninja=Join-Path $cmakeBase 'Ninja\ninja.exe'
if (!(Test-Path $cmake)) { $cmake=(Get-Command cmake -ErrorAction Stop).Source }
if (!(Test-Path $ninja)) { $ninja=(Get-Command ninja -ErrorAction Stop).Source }
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
if (!(Test-Path $ctest -PathType Leaf)) { throw 'CTest is missing from the CMake installation.' }
$env:INCLUDE="$($vc.FullName)\include;$($sdk.FullName)\ucrt;$($sdk.FullName)\shared;$($sdk.FullName)\um;$($sdk.FullName)\winrt;$($sdk.FullName)\cppwinrt"
$env:LIB="$($vc.FullName)\lib\x64;$WindowsSdkRoot\Lib\$($sdk.Name)\ucrt\x64;$WindowsSdkRoot\Lib\$($sdk.Name)\um\x64"
$env:PATH="$($vc.FullName)\bin\Hostx64\x64;$WindowsSdkRoot\bin\$($sdk.Name)\x64;$(Split-Path $cmake);$(Split-Path $ninja);$env:PATH"
$BuildTools=@{ CMake=$cmake; CTest=$ctest; Ninja=$ninja; RC="$WindowsSdkRoot/bin/$($sdk.Name)/x64/rc.exe"; MT="$WindowsSdkRoot/bin/$($sdk.Name)/x64/mt.exe" }

foreach ($key in @('RC','MT','Ninja')) { $BuildTools[$key]=$BuildTools[$key].Replace('\','/') }

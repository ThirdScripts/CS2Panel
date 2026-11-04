param([string]$QtRoot, [string]$BuildDirectory='build-stage1', [string]$VsInstallPath, [string]$WindowsSdkRoot, [int]$Jobs=4)
$ErrorActionPreference='Stop'
if ($Jobs -lt 1) { throw 'Jobs must be positive.' }
if (!$QtRoot) { $QtRoot=$env:QT_ROOT_DIR }
if (!$QtRoot) { $QtRoot=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Qt' }
if (!(Test-Path "$QtRoot\lib\cmake\Qt6\Qt6Config.cmake")) { throw 'Qt6 C++ development package not found. Supply -QtRoot or QT_ROOT_DIR.' }
$savedBuildEnvironment=@{}
foreach ($name in @('PATH','INCLUDE','LIB')) { $savedBuildEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
Push-Location $PSScriptRoot
try {
  . "$PSScriptRoot\..\tools\build-environment.ps1" -VsInstallPath $VsInstallPath -WindowsSdkRoot $WindowsSdkRoot
  $env:PATH="$QtRoot\bin;$env:PATH"
  & $BuildTools.CMake -S . -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_MAKE_PROGRAM=$($BuildTools.Ninja)" "-DCMAKE_RC_COMPILER=$($BuildTools.RC)" "-DCMAKE_MT=$($BuildTools.MT)"
  if ($LASTEXITCODE) { throw 'Panel CMake configure failed.' }
  & $BuildTools.CMake --build $BuildDirectory --parallel $Jobs
  if ($LASTEXITCODE) { throw 'Panel build failed.' }
  & $BuildTools.CTest --test-dir $BuildDirectory --output-on-failure --timeout 120
  if ($LASTEXITCODE) { throw 'Panel tests failed.' }
} finally {
  foreach ($name in $savedBuildEnvironment.Keys) {
    if ($null -eq $savedBuildEnvironment[$name]) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
    else { [Environment]::SetEnvironmentVariable($name,$savedBuildEnvironment[$name],'Process') }
  }
  Pop-Location
}

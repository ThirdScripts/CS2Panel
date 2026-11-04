param([string]$BuildDirectory='build-stage1-verified', [string]$VsInstallPath, [string]$WindowsSdkRoot, [int]$Jobs=4)
$ErrorActionPreference='Stop'
if ($Jobs -lt 1) { throw 'Jobs must be positive.' }
$savedBuildEnvironment=@{}
foreach ($name in @('PATH','INCLUDE','LIB')) { $savedBuildEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
Push-Location $PSScriptRoot
try {
  . "$PSScriptRoot\..\tools\build-environment.ps1" -VsInstallPath $VsInstallPath -WindowsSdkRoot $WindowsSdkRoot
  & $BuildTools.CMake -S . -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$($BuildTools.Ninja)" "-DCMAKE_RC_COMPILER=$($BuildTools.RC)" "-DCMAKE_MT=$($BuildTools.MT)"
  if ($LASTEXITCODE) { throw 'CMake configure failed.' }
  & $BuildTools.CMake --build $BuildDirectory --parallel $Jobs
  if ($LASTEXITCODE) { throw 'Build failed.' }
  & $BuildTools.CTest --test-dir $BuildDirectory --output-on-failure --timeout 120
  if ($LASTEXITCODE) { throw 'Tests failed.' }
} finally {
  foreach ($name in $savedBuildEnvironment.Keys) {
    if ($null -eq $savedBuildEnvironment[$name]) { Remove-Item -LiteralPath "Env:$name" -ErrorAction SilentlyContinue }
    else { [Environment]::SetEnvironmentVariable($name,$savedBuildEnvironment[$name],'Process') }
  }
  Pop-Location
}


param(
 [switch]$Install,
 [switch]$Recover,
 [uint32]$RuntimePid = 0,
 [string]$PanelDirectory = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent),
 [string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive'
)
$ErrorActionPreference='Stop'
$env:PYTHONUTF8='1'
if($Install -and $Recover){throw 'Use Install or Recover, not both.'}
$python=(Get-Command python -ErrorAction Stop).Source
$target=Join-Path $PanelDirectory 'bot\assets'
$tool=Join-Path $PSScriptRoot 'update.py'
if($Recover){
 & $python $tool recover --target $target
 if($LASTEXITCODE){throw 'Recovery refused or failed; see diagnostic above.'}
 exit
}
$settings=Join-Path $PanelDirectory 'panel-settings.ini'
if(!$PSBoundParameters.ContainsKey('GameDirectory') -and (Test-Path -LiteralPath $settings)){
 $section=''
 foreach($line in (Get-Content -LiteralPath $settings)){
  if($line -match '^\s*\[([^\]]+)\]\s*$'){$section=$Matches[1].Trim();continue}
  if($section -eq 'Paths' -and $line -match '^\s*GameDir\s*=\s*(.*?)\s*$'){
   $value=$Matches[1].Trim().Trim('"')
   if(!$value){throw 'GameDir in [Paths] is empty.'}
   $GameDirectory=$value;break
  }
 }
}
$output=Join-Path $PSScriptRoot ('runs\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
$runtimeArguments=@()
if($RuntimePid){$runtimeArguments=@('--runtime-pid',"$RuntimePid")}
& $python $tool prepare --game $GameDirectory --assets (Join-Path $PSScriptRoot 'approved') --output $output @runtimeArguments
$code=$LASTEXITCODE
Write-Output "Report: $output"
if($code -eq 2){Write-Warning 'New dependencies require validation. Installed profiles were not modified.';exit 2}
if($code){throw 'Preparation failed.'}
if($Install){
 & $python $tool install --package $output --target $target
 if($LASTEXITCODE){throw 'Installation refused or rolled back; see diagnostic above.'}
}

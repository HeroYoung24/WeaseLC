param([Parameter(Mandatory=$true)][ValidateSet('Install','Unregister')][string]$Action)
$ErrorActionPreference='Stop'
$source=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$report=Join-Path $source 'output\LocalTest'
$result=Join-Path $report ('elevated-'+$Action+'.json')
$code=1;$message=''
Start-Transcript -LiteralPath (Join-Path $report ('elevated-'+$Action+'.log')) -Force
try{
  if($Action -eq 'Install'){& (Join-Path $PSScriptRoot 'Install.ps1') -Action Install -ValidateRoundTrip}
  else{& (Join-Path $PSScriptRoot 'Install.ps1') -Action Unregister}
  $code=0
}catch{$message=$_.Exception.ToString();Write-Output $message}
finally{
  @{action=$Action;exitCode=$code;message=$message;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $result -Encoding UTF8
  Stop-Transcript
}
exit $code

#requires -Version 7.0
param([Parameter(Mandatory)][string]$Patch)
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$patchRoot = [IO.Path]::GetFullPath($Patch).TrimEnd('\')
if ([IO.Path]::GetDirectoryName($patchRoot) -ne (Join-Path $source 'output\LocalTest') -or
    ![IO.Path]::GetFileName($patchRoot).StartsWith('UpdatePatch-',[StringComparison]::Ordinal)) { throw 'Wrong workspace patch location.' }
$result = Join-Path $patchRoot 'elevated-result.json'
if (Test-Path -LiteralPath $result) { throw 'Preserve previous update result.' }
$code = 1; $message = ''
Start-Transcript -LiteralPath (Join-Path $patchRoot 'elevated.log') -NoClobber
try {
  & (Join-Path $PSScriptRoot 'Update-LocalTest.ps1') -Patch $patchRoot -ValidateOnly
  & (Join-Path $PSScriptRoot 'Update-LocalTest.ps1') -Patch $patchRoot -RehearseRollback
  $rehearsal = Get-Content -LiteralPath (Join-Path $patchRoot 'result-rehearsal.json') -Raw | ConvertFrom-Json
  if (!$rehearsal.restored -or $rehearsal.error -or !$rehearsal.dailyBaselineUnchanged) { throw 'Rollback rehearsal failed; install was not advanced.' }
  & (Join-Path $PSScriptRoot 'Update-LocalTest.ps1') -Patch $patchRoot
  $applied = Get-Content -LiteralPath (Join-Path $patchRoot 'result-apply.json') -Raw | ConvertFrom-Json
  if (!$applied.applied -or $applied.error -or !$applied.dailyBaselineUnchanged) { throw 'Update not verified.' }
  $code = 0
} catch { $message = $_.Exception.ToString(); Write-Output $message }
finally {
  @{exitCode=$code;message=$message;utc=[DateTime]::UtcNow.ToString('o')} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $result -Encoding utf8
  Stop-Transcript
}
exit $code

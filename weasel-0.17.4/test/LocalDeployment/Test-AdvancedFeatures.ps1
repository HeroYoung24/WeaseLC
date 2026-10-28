#requires -Version 7.0
param([ValidateSet('x64','Win32')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$run = Join-Path $source ('output\AdvancedFeatures-20261028\Run-' + $Architecture + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $run | Out-Null
$results = [Collections.Generic.List[object]]::new()
function Check-Owned([string]$Name, [string]$Executable, [string[]]$Arguments, [string]$Directory = $source) {
  $info = [Diagnostics.ProcessStartInfo]::new($Executable)
  $info.UseShellExecute = $false; $info.CreateNoWindow = $true; $info.WorkingDirectory = $Directory
  $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
  foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
  $process = [Diagnostics.Process]::Start($info)
  try {
    $out = $process.StandardOutput.ReadToEndAsync(); $err = $process.StandardError.ReadToEndAsync()
    if (!$process.WaitForExit(60000)) {
      $process.Kill(); $process.WaitForExit(); throw "Owned test timed out: $Name"
    }
    $stdout = $out.GetAwaiter().GetResult(); $stderr = $err.GetAwaiter().GetResult()
    $stdout | Set-Content -LiteralPath (Join-Path $run "$Name.stdout.log")
    $stderr | Set-Content -LiteralPath (Join-Path $run "$Name.stderr.log")
    $results.Add(@{name=$Name;exitCode=$process.ExitCode})
    $results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'results.json')
    Write-Output "$Name exit=$($process.ExitCode)"
    if ($process.ExitCode) { Write-Output (($stdout -split '\r?\n' | Where-Object {$_ -match 'FAIL|failures|error'}) -join "`n"); throw "Check failed: $Name; logs in $run" }
  } finally { $process.Dispose() }
}
$deployer = Join-Path $source "output\LocalTest\$Architecture\WeaselDeployer.exe"
$integration = Join-Path $source "output\CorrectionIntegration\$Architecture\CorrectionIntegration.exe"
$probe = Join-Path $source "output\Probe\v3\$Architecture\weaselProbe.dll"
$activation = Join-Path $source "output\ProbeDiagnostics\$Architecture\ProbeActivation.exe"
$broker = Join-Path $source "output\BrokerTests\$Architecture\BrokerCheck.exe"
Check-Owned 'ui' $deployer @('/correction-selftest') $run
$ui = Get-Content (Join-Path $run 'CorrectionDialogChecks.log')
if ($ui -match ' = 0$') { throw 'UI assertion failed' }
Check-Owned 'learning-punctuation' $deployer @('/rime-learning-selftest')
Move-Item -LiteralPath (Join-Path $source 'RimeLearningChecks.log') -Destination (Join-Path $run 'RimeLearningChecks.log')
Check-Owned 'client' (Join-Path $source "output\CorrectionTests\$Architecture\CorrectionClient.exe") @()
Check-Owned 'automatic-host' $integration @($probe,'--tab-host-review')
Check-Owned 'capture-policy' $integration @($probe,'--word-memory-review')
Check-Owned 'offline-integration' $integration @($probe)
Check-Owned 'scope-policy' $activation @('--scope-policy')
Check-Owned 'composition' $activation @($probe,'{B749B45E-7DCE-4496-8414-9EE10970260A}','--composition-checks')
Check-Owned 'broker' $broker @()
Check-Owned 'supervisor' $broker @('--supervisor')
Check-Owned 'service-control' $broker @('--service-control')
Check-Owned 'listener' $broker @('--listener-lifecycle')
Check-Owned 'owned-model' $broker @('--owned-model',(Get-Command pwsh).Source,(Join-Path $PSScriptRoot 'ManagedModelPipeFixture.ps1'))
Write-Output "All checks passed; $run"

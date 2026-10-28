#requires -Version 7.0
param([ValidateSet('x64','Win32')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$modelRoot = Join-Path (Split-Path $source -Parent) 'local-model'
$run = Join-Path $source ('output\AdvancedFeatures-20261028\RealModel-' + $Architecture + '-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
if (Test-Path -LiteralPath (Join-Path $modelRoot 'ready.json')) { throw 'Existing ready file: refusing controlled model test.' }
New-Item -ItemType Directory -Path $run | Out-Null
foreach ($name in @('runtime.stdout.log','runtime.stderr.log')) {
  if (Test-Path -LiteralPath (Join-Path $modelRoot $name)) {
    Copy-Item -LiteralPath (Join-Path $modelRoot $name) -Destination (Join-Path $run ('previous-' + $name))
  }
}
$info = [Diagnostics.ProcessStartInfo]::new((Join-Path $source "output\LocalTest\$Architecture\WeaselDeployer.exe"), '/local-model-selftest')
$info.UseShellExecute=$false; $info.CreateNoWindow=$true; $info.WorkingDirectory=$run
$info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
$info.Environment['WEASELC_MODEL_TEST_PWSH'] = (Get-Command pwsh).Source
$process = [Diagnostics.Process]::Start($info)
try {
  $out = $process.StandardOutput.ReadToEndAsync(); $err = $process.StandardError.ReadToEndAsync()
  if (!$process.WaitForExit(150000)) { $process.Kill(); $process.WaitForExit(); throw 'Owned model check timed out.' }
  $out.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $run 'stdout.log')
  $err.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $run 'stderr.log')
  foreach ($name in @('runtime.stdout.log','runtime.stderr.log')) {
    if (Test-Path -LiteralPath (Join-Path $modelRoot $name)) {
      Copy-Item -LiteralPath (Join-Path $modelRoot $name) -Destination (Join-Path $run $name)
    }
  }
  if (Test-Path -LiteralPath (Join-Path $run 'ManagedModelChecks.log')) { Get-Content (Join-Path $run 'ManagedModelChecks.log') }
  $summary = @{exitCode=$process.ExitCode;readyFileRemains=(Test-Path -LiteralPath (Join-Path $modelRoot 'ready.json'));report=$run}
  $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'summary.json')
  $summary | ConvertTo-Json -Compress
  if ($process.ExitCode -or $summary.readyFileRemains) { throw 'Managed model check failed; see report.' }
} finally { $process.Dispose() }

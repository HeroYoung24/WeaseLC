#requires -Version 7.0
param([Parameter(Mandatory)][ValidateSet('4b','7b')][string]$Variant)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$models = Join-Path (Split-Path $source -Parent) 'local-model'
$ready = Get-Content -LiteralPath (Join-Path $models 'ready.json') -Raw | ConvertFrom-Json
if ($ready.variant -ne $Variant -or $ready.thinkingBudget -ne 0 -or $ready.port -ne 18963) { throw 'Unexpected model/budget/port.' }
$owned = Get-CimInstance Win32_Process -Filter "ProcessId = $($ready.processId)"
if (!$owned -or $owned.ExecutablePath -ne (Join-Path $models 'runtime\llama-server.exe') -or
    !$owned.CommandLine.Contains($ready.modelFile)) { throw 'Unexpected runtime identity.' }
$null = Invoke-RestMethod -Uri 'http://127.0.0.1:18963/health' -NoProxy -TimeoutSec 3
$runner = Join-Path $source 'output\CorrectionTests\x64\CorrectionClient.exe'
$directory = Join-Path $source 'output\LocalTest'
$stem = "candidate-guard-$Variant-20261022"
$reportPath = Join-Path $directory "$stem.json"
$runtimePath = Join-Path $directory "$stem-runtime.json"
$logPath = Join-Path $directory "$stem.log"
foreach ($path in @($reportPath,$runtimePath,$logPath)) {
  if (Test-Path -LiteralPath $path) { throw "Keep earlier artifact: $path" }
}
& $runner --quality 'http://127.0.0.1:18963/v1/chat/completions' $ready.alias current candidate-guard $reportPath greedy > $logPath
if ($LASTEXITCODE -gt 1) { throw "Quality runner error: $LASTEXITCODE" }
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if ($report.corpus -ne 'candidate-guard' -or $report.samples.Count -ne 40 -or $report.threshold -ne .9 -or
    $report.profile -ne 'greedy' -or $report.model -ne $ready.alias) { throw 'Unexpected report.' }
$slots = @(Invoke-RestMethod -Uri 'http://127.0.0.1:18963/slots' -NoProxy -TimeoutSec 3)
if ($slots.Count -ne 1 -or $slots[0].is_processing) { throw 'Unexpected active slots.' }
$effective = $slots[0].params
if ($effective.temperature -ne 0 -or $effective.max_tokens -ne 768) { throw 'Actual parameters differ.' }
[ordered]@{variant=$Variant;thinkingBudget=0;profile='greedy';verified=$true;
  modelSha256=$ready.modelSha256;runtimeSha256=(Get-FileHash -LiteralPath $owned.ExecutablePath).Hash;
  corpusSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'CandidateGuardCorpus.h')).Hash;
  guardSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'EvaluateCandidateGuard.py')).Hash;
  parameters=($effective | Select-Object temperature,top_p,top_k,min_p,seed,max_tokens)} |
  ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $runtimePath
$report | Select-Object model,corrected,wrongCorrections,falseAlarms,invalid,medianMs,p95Ms,maxMs | ConvertTo-Json -Compress

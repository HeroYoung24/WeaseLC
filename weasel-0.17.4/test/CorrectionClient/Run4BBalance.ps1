#requires -Version 7.0
param(
  [Parameter(Mandatory)][ValidateSet('tuning-development','candidate-guard','balance-confirmation')][string]$Corpus,
  [Alias('Profile')][ValidateSet('greedy','typical-020')][string]$SamplingProfile = 'greedy',
  [uint32]$Seed = 104729
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$models = Join-Path (Split-Path $source -Parent) 'local-model'
$ready = Get-Content -LiteralPath (Join-Path $models 'ready.json') -Raw | ConvertFrom-Json
if ($ready.variant -ne '4b' -or $ready.alias -ne 'weasel-qwen3-4b' -or
    $ready.thinkingBudget -ne 0 -or $ready.port -ne 18963) { throw 'Expected original non-thinking 4B on the test port.' }
$owned = Get-CimInstance Win32_Process -Filter "ProcessId = $($ready.processId)"
if (!$owned -or $owned.ExecutablePath -ne (Join-Path $models 'runtime\llama-server.exe') -or
    !$owned.CommandLine.Contains($ready.modelFile)) { throw 'Unexpected runtime identity.' }
$null = Invoke-RestMethod -Uri 'http://127.0.0.1:18963/health' -NoProxy -TimeoutSec 3
$directory = Join-Path $source 'output\LocalTest'
$stem = "balance-4b-$Corpus-$SamplingProfile-s$Seed-20261022"
$reportPath = Join-Path $directory "$stem.json"
$runtimePath = Join-Path $directory "$stem-runtime.json"
$logPath = Join-Path $directory "$stem.log"
foreach ($path in @($reportPath,$runtimePath,$logPath)) {
  if (Test-Path -LiteralPath $path) { throw "Preserve earlier artifact: $path" }
}
$runner = Join-Path $source 'output\CorrectionTests\x64\CorrectionClient.exe'
& $runner --quality 'http://127.0.0.1:18963/v1/chat/completions' $ready.alias current $Corpus $reportPath $SamplingProfile $Seed > $logPath
if ($LASTEXITCODE -gt 1) { throw "Quality runner error: $LASTEXITCODE" }
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$slots = @(Invoke-RestMethod -Uri 'http://127.0.0.1:18963/slots' -NoProxy -TimeoutSec 3)
if ($slots.Count -ne 1 -or $slots[0].is_processing) { throw 'Unexpected active slots.' }
$effective = $slots[0].params
$expected = if ($SamplingProfile -eq 'greedy') { @{temperature=0;max_tokens=768} } else {
  @{temperature=.7;typical_p=.2;top_p=1;top_k=0;min_p=0;seed=$Seed;max_tokens=768}
}
foreach ($key in $expected.Keys) {
  if (!$effective.PSObject.Properties[$key] -or [Math]::Abs([double]$effective.$key - [double]$expected[$key]) -gt .000001) {
    throw "Actual parameter mismatch: $key"
  }
  if (!$report.requestParameters.PSObject.Properties[$key] -or
      [Math]::Abs([double]$report.requestParameters.$key - [double]$expected[$key]) -gt .000001) {
    throw "Sent parameter mismatch: $key"
  }
}
$expectedCount = if ($Corpus -eq 'tuning-development') {70} elseif ($Corpus -eq 'candidate-guard') {40} else {32}
if ($report.corpus -ne $Corpus -or $report.samples.Count -ne $expectedCount -or
    $report.model -ne $ready.alias -or $report.profile -ne $SamplingProfile -or $report.threshold -ne .9) { throw 'Unexpected report.' }
[ordered]@{variant='4b';thinkingBudget=0;profile=$SamplingProfile;verified=$true;
  modelSha256=$ready.modelSha256;runtimeSha256=(Get-FileHash -LiteralPath $owned.ExecutablePath).Hash;
  runnerSha256=(Get-FileHash -LiteralPath $runner).Hash;
  confirmationCorpusSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'BalanceConfirmationCorpus.h')).Hash;
  evaluatorSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'Evaluate4BBalance.py')).Hash;
  parameters=$effective} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $runtimePath
$report | Select-Object model,corpus,profile,corrected,wrongCorrections,falseAlarms,invalid,medianMs,p95Ms,maxMs | ConvertTo-Json -Compress

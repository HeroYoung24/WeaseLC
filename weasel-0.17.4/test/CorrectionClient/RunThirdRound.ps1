#requires -Version 7.0
param(
  [Parameter(Mandatory)][ValidateSet('4b','7b')][string]$Variant,
  [ValidateSet(0,16,24)][int]$ThinkingBudget = 0,
  [Parameter(Mandatory)][ValidateSet('greedy','typical-020','minp-015','mirostat-3','thinking-16-greedy','thinking-24-greedy')][string[]]$Profiles,
  [uint32]$Seed = 104729
)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$models = Join-Path (Split-Path $source -Parent) 'local-model'
$ready = Get-Content -LiteralPath (Join-Path $models 'ready.json') -Raw | ConvertFrom-Json
if ($ready.variant -ne $Variant -or $ready.thinkingBudget -ne $ThinkingBudget -or $ready.port -ne 18963) { throw 'Unexpected model/budget/port.' }
$owned = Get-CimInstance Win32_Process -Filter "ProcessId = $($ready.processId)"
if (!$owned -or $owned.ExecutablePath -ne (Join-Path $models 'runtime\llama-server.exe') -or
    !$owned.CommandLine.Contains($ready.modelFile)) { throw 'Unexpected runtime identity.' }
$null = Invoke-RestMethod -Uri 'http://127.0.0.1:18963/health' -NoProxy -TimeoutSec 3
$runner = Join-Path $source 'output\CorrectionTests\x64\CorrectionClient.exe'
$directory = Join-Path $source 'output\LocalTest'
foreach ($profile in $Profiles) {
  $expectedBudget = if ($profile -eq 'thinking-16-greedy') {16} elseif ($profile -eq 'thinking-24-greedy') {24} else {0}
  if ($ThinkingBudget -ne $expectedBudget) { throw 'Profile does not match the actual reasoning budget.' }
  $stem = "tune3-$Variant-$profile-s$Seed-20261022"
  $reportPath = Join-Path $directory "$stem.json"
  $runtimePath = Join-Path $directory "$stem-runtime.json"
  $logPath = Join-Path $directory "$stem.log"
  foreach ($path in @($reportPath,$runtimePath,$logPath)) { if (Test-Path -LiteralPath $path) { throw "Keep earlier artifact: $path" } }
  & $runner --quality 'http://127.0.0.1:18963/v1/chat/completions' $ready.alias current tuning-development $reportPath $profile $Seed > $logPath
  if ($LASTEXITCODE -gt 1) { throw "Quality runner error: $LASTEXITCODE" }
  $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
  $slots = @(Invoke-RestMethod -Uri 'http://127.0.0.1:18963/slots' -NoProxy -TimeoutSec 3)
  if ($slots.Count -ne 1 -or $slots[0].is_processing) { throw 'Unexpected active slots.' }
  $effective = $slots[0].params
  $verified = $true
  $keys = @('temperature','top_p','top_k','min_p','typical_p','mirostat','mirostat_tau','mirostat_eta','seed','max_tokens')
  foreach ($key in $keys) {
    $sent = $report.requestParameters.PSObject.Properties[$key]
    if ($sent) {
      $actual = $effective.PSObject.Properties[$key]
      if (!$actual -or [Math]::Abs([double]$sent.Value - [double]$actual.Value) -gt 0.000001) { $verified = $false }
    }
  }
  [ordered]@{variant=$Variant;thinkingBudget=$ThinkingBudget;profile=$profile;seed=$Seed;verified=$verified;
    modelSha256=$ready.modelSha256;parameters=($effective | Select-Object $keys)} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $runtimePath
  if (!$verified) { throw 'Actual sampling parameters differ from the request; do not score this trial.' }
  $report | Select-Object model,profile,corrected,wrongCorrections,falseAlarms,invalid,medianMs,p95Ms,maxMs | ConvertTo-Json -Compress
}

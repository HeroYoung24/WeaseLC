#requires -Version 7.0
param(
  [Parameter(Mandatory)][string]$Patch,
  [switch]$ValidateOnly,
  [switch]$RehearseRollback
)
. (Join-Path $PSScriptRoot 'Common.ps1')
if ($ValidateOnly -and $RehearseRollback) { throw 'Choose validation or rollback rehearsal.' }
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$outputRoot = Join-Path $source 'output\LocalTest'
$patchRoot = [IO.Path]::GetFullPath($Patch).TrimEnd('\')
if ([IO.Path]::GetDirectoryName($patchRoot) -ne $outputRoot -or
    ![IO.Path]::GetFileName($patchRoot).StartsWith('UpdatePatch-',[StringComparison]::Ordinal)) { throw 'Use a dedicated workspace UpdatePatch-* directory.' }
$installRoot = $script:TestRoot
$manifestPath = Join-Path $installRoot 'manifest.json'
$allowed = @('WeaselServer.exe','WeaselDeployer.exe','weaselx64.dll',
             'Win32\WeaselServer.exe','Win32\WeaselDeployer.exe','Win32\weasel.dll')
foreach ($tree in @($patchRoot,$installRoot)) {
  foreach ($entry in @((Get-Item -LiteralPath $tree)) + @(Get-ChildItem -LiteralPath $tree -Recurse -Force)) {
    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not allowed.' }
  }
}
$spec = Get-Content -LiteralPath (Join-Path $patchRoot 'patch.json') -Raw | ConvertFrom-Json
if ($spec.format -ne 1 -or $spec.purpose -ne 'weaselc-six-file-update' -or
    $spec.baseManifestSha256 -notmatch '^[a-fA-F0-9]{64}$' -or
    $spec.sourceCommit -notmatch '^[a-fA-F0-9]{40}$') { throw 'Unexpected patch identity.' }
if (@($spec.files).Count -ne 6 -or @($spec.files.path | Sort-Object -Unique).Count -ne 6) { throw 'Exactly six unique replacements required.' }
if ((Get-FileHash -LiteralPath $manifestPath).Hash -ne $spec.baseManifestSha256) { throw 'Installed base manifest changed.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.format -ne 1 -or $manifest.purpose -ne 'weaselc-side-by-side-local-test') { throw 'Wrong installed product.' }

function Hash-Bytes([byte[]]$Bytes) {
  $algorithm = [Security.Cryptography.SHA256]::Create()
  try { return [Convert]::ToHexString($algorithm.ComputeHash($Bytes)) } finally { $algorithm.Dispose() }
}
function Read-Stream([IO.Stream]$Stream) {
  if ($Stream.Length -gt 20MB) { throw 'Unexpected file size.' }
  $Stream.Position = 0
  $buffer = [IO.MemoryStream]::new()
  try { $Stream.CopyTo($buffer); return ,$buffer.ToArray() } finally { $buffer.Dispose() }
}
function Write-Stream([IO.FileStream]$Stream, [byte[]]$Bytes) {
  $Stream.Position = 0
  $Stream.Write($Bytes,0,$Bytes.Length)
  $Stream.SetLength($Bytes.Length)
  $Stream.Flush($true)
}
$newBytes = @{}
foreach ($file in $spec.files) {
  if ($file.path -cnotin $allowed -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or
      $file.previousSha256 -notmatch '^[a-fA-F0-9]{64}$' -or $file.bytes -le 0 -or $file.bytes -gt 20MB) { throw 'Unexpected replacement.' }
  $incoming = Join-Path (Join-Path $patchRoot 'new') $file.path
  $target = Join-Path $installRoot $file.path
  $payload = [IO.File]::ReadAllBytes($incoming)
  if ($payload.Length -ne $file.bytes -or (Hash-Bytes $payload) -ne $file.sha256 -or
      (Get-FileHash -LiteralPath $target).Hash -ne $file.previousSha256) { throw 'Replacement/base hash mismatch.' }
  if ($payload.Length -lt 256 -or $payload[0] -ne 0x4d -or $payload[1] -ne 0x5a) { throw 'Not a PE binary.' }
  $peOffset = [BitConverter]::ToInt32($payload,0x3c)
  if ($peOffset -lt 0 -or $peOffset -gt $payload.Length-24 -or [BitConverter]::ToUInt32($payload,$peOffset) -ne 0x4550) { throw 'Invalid PE header.' }
  $machine = if ($file.path.StartsWith('Win32\')) {0x14c} else {0x8664}
  if ([BitConverter]::ToUInt16($payload,$peOffset+4) -ne $machine) { throw 'Wrong architecture.' }
  $newBytes[$target] = $payload
}
$handles = @{}
function Assert-Manifest($Expected) {
  $listed = @{}
  foreach ($file in $Expected.files) {
    if ($file.path -notmatch '^App\\' -or $file.path -match '(^|[\\/])\.\.([\\/]|$)|:') { throw 'Unsafe manifest path.' }
    $target = [IO.Path]::GetFullPath((Join-Path $installRoot $file.path.Substring(4)))
    if (!$target.StartsWith($installRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or $listed.ContainsKey($target)) { throw 'Duplicate/escaping manifest path.' }
    $listed[$target] = $true
    $actualHash = if ($handles.ContainsKey($target)) {Hash-Bytes (Read-Stream $handles[$target])} else {(Get-FileHash -LiteralPath $target).Hash}
    if ((Get-Item -LiteralPath $target).Length -ne $file.bytes -or $actualHash -ne $file.sha256) { throw ('Installed file mismatch: '+$file.path) }
  }
  $actual = @(Get-ChildItem -LiteralPath $installRoot -Recurse -File | Where-Object {
    $_.FullName -ne $manifestPath -and $_.FullName -ne (Join-Path $installRoot 'daily-baseline.json')
  })
  if ($actual.Count -ne $listed.Count) { throw 'Unexpected installed file count.' }
  foreach ($file in $actual) { if (!$listed.ContainsKey($file.FullName)) { throw 'Unlisted installed file.' } }
}
Assert-Manifest $manifest
$baseline = Get-DailyBaseline
if ($baseline -ne (Get-Content -LiteralPath (Join-Path $installRoot 'daily-baseline.json') -Raw).Trim()) { throw 'Original input method baseline changed.' }
if ($ValidateOnly) { Write-Output 'Six-file patch, architectures and complete installed base validated.'; return }
if (!(Test-Admin)) { throw 'Windows UAC administrator token required to update the dedicated install.' }
foreach ($process in Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('WeaselServer.exe','WeaselDeployer.exe')}) {
  if (!$process.ExecutablePath) { throw 'Cannot identify a backend process.' }
  if ($process.ExecutablePath.StartsWith($installRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Stop the independent backend before updating.' }
}
foreach ($process in Get-Process) {
  try { $modules = @($process.Modules) } catch { $modules = @() }
  foreach ($module in $modules) {
    if ($module -and $module.FileName.StartsWith($installRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw ('Test module is still loaded in PID '+$process.Id) }
  }
}
$suffix = if ($RehearseRollback) {'rehearsal'} else {'apply'}
$backup = Join-Path $patchRoot ('backup-'+$suffix)
$resultPath = Join-Path $patchRoot ('result-'+$suffix+'.json')
if ((Test-Path -LiteralPath $backup) -or (Test-Path -LiteralPath $resultPath)) { throw 'Preserve prior attempt/backup.' }
$originalBytes = @{}
$mutated = $false; $applied = $false; $restored = $false; $failure = ''; $rollbackErrors = @()
try {
  # Acquire EVERY target and the manifest before the first write. Sharing denial
  # also prevents a new host from loading a DLL during the transaction.
  foreach ($target in @($newBytes.Keys) + @($manifestPath) | Sort-Object) {
    $handles[$target] = [IO.File]::Open($target,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $originalBytes[$target] = Read-Stream $handles[$target]
  }
  if ((Hash-Bytes $originalBytes[$manifestPath]) -ne $spec.baseManifestSha256) { throw 'Base changed while acquiring locks.' }
  foreach ($file in $spec.files) {
    if ((Hash-Bytes $originalBytes[(Join-Path $installRoot $file.path)]) -ne $file.previousSha256) { throw 'Base binary changed while acquiring locks.' }
  }
  $null = New-Item -ItemType Directory -Path (Join-Path $backup 'Win32')
  foreach ($target in $originalBytes.Keys) {
    $relative = $target.Substring($installRoot.Length+1)
    $saved = Join-Path $backup $relative
    [IO.File]::WriteAllBytes($saved,$originalBytes[$target])
    if ((Get-FileHash -LiteralPath $saved).Hash -ne (Hash-Bytes $originalBytes[$target])) { throw 'Backup verification failed.' }
  }
  $mutated = $true
  foreach ($file in $spec.files) {
    $target = Join-Path $installRoot $file.path
    Write-Stream $handles[$target] $newBytes[$target]
    $entries = @($manifest.files | Where-Object {$_.path -ceq ('App\'+$file.path)})
    if ($entries.Count -ne 1) { throw 'Replacement missing from manifest.' }
    $entries[0].sha256 = $file.sha256; $entries[0].bytes = $file.bytes
  }
  Assert-Manifest $manifest
  if ((Get-DailyBaseline) -ne $baseline) { throw 'Original input method baseline changed.' }
  $manifestBytes = [Text.UTF8Encoding]::new($false).GetBytes(($manifest | ConvertTo-Json -Depth 6))
  Write-Stream $handles[$manifestPath] $manifestBytes
  if ((Hash-Bytes (Read-Stream $handles[$manifestPath])) -ne (Hash-Bytes $manifestBytes)) { throw 'Manifest write verification failed.' }
  if (!$RehearseRollback) { $applied = $true }
} catch {
  $failure = $_.Exception.ToString()
} finally {
  if ($mutated -and (!$applied -or $RehearseRollback)) {
    foreach ($target in $originalBytes.Keys) {
      try {
        Write-Stream $handles[$target] $originalBytes[$target]
        if ((Hash-Bytes (Read-Stream $handles[$target])) -ne (Hash-Bytes $originalBytes[$target])) { throw 'Restore hash mismatch.' }
      } catch { $rollbackErrors += ($target+': '+$_.Exception.Message) }
    }
    $restored = $rollbackErrors.Count -eq 0
  }
  foreach ($handle in $handles.Values) { $handle.Dispose() }
  $handles.Clear()
  @{sourceCommit=$spec.sourceCommit;applied=$applied;rehearsal=[bool]$RehearseRollback;restored=$restored;
    error=$failure;rollbackErrors=$rollbackErrors;files=@($spec.files.path);utc=[DateTime]::UtcNow.ToString('o');
    dailyBaselineUnchanged=((Get-DailyBaseline) -eq $baseline)} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $resultPath -Encoding UTF8
}
if ($failure) { throw $failure }
if ($rollbackErrors.Count) { throw ('Rollback incomplete: '+($rollbackErrors -join '; ')) }
$expectedManifest = if ($RehearseRollback) {Get-Content -LiteralPath (Join-Path $backup 'manifest.json') -Raw | ConvertFrom-Json} else {$manifest}
Assert-Manifest $expectedManifest
if ((Get-DailyBaseline) -ne $baseline) { throw 'Final original baseline differs.' }
if ($RehearseRollback -and !$restored) { throw 'Rollback rehearsal did not restore the base.' }
Write-Output $(if ($RehearseRollback) {'Six-file rollback rehearsal verified.'} else {'Updated dedicated Server/Deployer/TSF in both architectures; registration and dictionaries unchanged.'})

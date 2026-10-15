$ErrorActionPreference = 'Stop'
$key = 'HKEY_CURRENT_USER\Software\Rime\Weasel\Correction'
$raw = [Microsoft.Win32.Registry]::GetValue($key, 'LastProbe', $null)

if ($null -eq $raw) {
  Write-Output 'NotRun: no probe result has been recorded for this Windows user.'
  exit 0
}
if ($raw -isnot [byte[]] -or $raw.Length -ne 16) {
  throw 'LastProbe has an unexpected format; expected four DWORD values.'
}

$version = [BitConverter]::ToUInt32($raw, 0)
$status = [BitConverter]::ToUInt32($raw, 4)
$before = [BitConverter]::ToUInt32($raw, 8)
$after = [BitConverter]::ToUInt32($raw, 12)
$names = @(
  'NotRun',
  'AwaitingEdit',
  'Readable',
  'NoContext',
  'FocusChanged',
  'ScopeUnavailable',
  'ProtectedField',
  'SelectionUnavailable',
  'ReadFailed'
)
if ($version -ne 1 -or $status -ge $names.Length -or
    $before -gt 64 -or $after -gt 64) {
  throw 'LastProbe contains an unsupported version or invalid values.'
}

[pscustomobject]@{
  Status = $names[$status]
  CharactersBeforeCaret = $before
  CharactersAfterCaret = $after
  TextStored = $false
} | Format-List

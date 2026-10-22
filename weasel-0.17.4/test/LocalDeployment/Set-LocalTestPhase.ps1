#requires -Version 7.0
param([Parameter(Mandatory)][ValidateSet('Off','Fixed','Model')][string]$Phase)
$ErrorActionPreference = 'Stop'
# Dedicated test identity only. Never touch ordinary Weasel/Sogou settings.
$path = 'Software\Rime\WeaseLC-LocalTest\Correction'
$key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($path,$true)
if (!$key) { throw 'Existing independent test settings required.' }
try {
  $raw = $key.GetValue('SettingsV2',$null)
  if ($raw -isnot [byte[]] -or !$raw.Length -or $raw.Length -gt 16384) { throw 'Unexpected settings value.' }
  $settings = [Text.Encoding]::UTF8.GetString($raw) | ConvertFrom-Json
  if ($settings.version -ne 3) { throw 'Version 3 settings required.' }
  $settings.mode = if ($Phase -eq 'Off') {0} else {1}
  $settings.automatic.enabled = $Phase -ne 'Off'
  if ($Phase -ne 'Off') {
    $settings.timing = 0
    $settings.automatic.tab = $true
    $settings.local.endpoint = if ($Phase -eq 'Model') {'http://127.0.0.1:18963/v1/chat/completions'} else {'http://127.0.0.1:9/v1/chat/completions'}
    $settings.local.model = if ($Phase -eq 'Model') {'weasel-qwen3-4b'} else {'compiled-fixture'}
    $settings.local.timeout = 5
    $settings.local.sensitivity = 0
    $settings.local.custom_threshold = $false
    $settings.local.threshold = .9
    $settings.local.json_mode = $true
  }
  $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($settings | ConvertTo-Json -Depth 6 -Compress))
  if ($bytes.Length -gt 16384) { throw 'Settings too large.' }
  $diagnostics = $key.CreateSubKey('Diagnostics')
  try { $diagnostics.SetValue('UseFixedReplyV1',[int]($Phase -eq 'Fixed'),[Microsoft.Win32.RegistryValueKind]::DWord) }
  finally { $diagnostics.Dispose() }
  $key.SetValue('SettingsV2',$bytes,[Microsoft.Win32.RegistryValueKind]::Binary)
  if ([Convert]::ToBase64String($key.GetValue('SettingsV2')) -cne [Convert]::ToBase64String($bytes)) { throw 'Settings readback differs.' }
} finally { $key.Dispose() }
[pscustomobject]@{phase=$Phase;mode=$settings.mode;automatic=$settings.automatic.enabled;threshold=.9;fixedReply=($Phase -eq 'Fixed')} | ConvertTo-Json -Compress

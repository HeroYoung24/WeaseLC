param(
  [ValidateSet('cycles', 'input')]
  [string]$Mode = 'cycles',
  [ValidateRange(1, 10)]
  [int]$Count = 1,
  [switch]$Transcript,
  [switch]$HiddenProfile,
  [switch]$InputAfterCycles
)

$ErrorActionPreference = 'Stop'
$probeClsid = '{B749B45E-7DCE-4496-8414-9EE10970260A}'
$originalClsid = '{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}'
$tipPath = "Software\Microsoft\CTF\TIP\$probeClsid"
$comPath = "Software\Classes\CLSID\$probeClsid"
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$executable = Join-Path $sourceRoot 'output\ProbeDiagnostics\x64\ProbeActivation.exe'
$dll = Join-Path $sourceRoot 'output\ProbeDiagnostics\x64\weaselProbe.dll'
if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $dll)) {
  throw 'Build the x64 probe and diagnostic first.'
}
if ($Mode -eq 'input' -and $Count -ne 1) { throw 'The interactive mode runs once.' }
if ($InputAfterCycles -and $Mode -ne 'cycles') { throw 'InputAfterCycles requires cycles mode.' }

function Get-InputState {
  $paths = @()
  foreach ($view in @('Registry32', 'Registry64')) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey('ClassesRoot', $view)
    try {
      $key = $base.OpenSubKey("CLSID\$originalClsid\InprocServer32")
      try { $paths += if ($key) { [string]$key.GetValue('') } else { '' } }
      finally { if ($key) { $key.Dispose() } }
    } finally { $base.Dispose() }
  }
  $tips = @(Get-WinUserLanguageList | ForEach-Object {
    $_.LanguageTag
    $_.InputMethodTips
  })
  return [string]::Join('|', @($paths) + @($tips))
}

function Assert-ProbeAbsent {
  foreach ($hive in @('LocalMachine', 'CurrentUser')) {
    foreach ($view in @('Registry32', 'Registry64')) {
      $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, $view)
      try {
        foreach ($path in @($tipPath, $comPath)) {
          $key = $base.OpenSubKey($path)
          if ($key) {
            $key.Dispose()
            throw "Existing probe registration: $hive/$view/$path. No changes made."
          }
        }
      } finally { $base.Dispose() }
    }
  }
}

function Remove-TestCategoryRoots {
  # The preflight requires these exact roots to be absent. Never enumerate and
  # delete other input methods. Refuse unexpected COM/profile registration.
  foreach ($hive in @('LocalMachine', 'CurrentUser')) {
    foreach ($view in @('Registry32', 'Registry64')) {
      $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, $view)
      try {
        $key = $base.OpenSubKey($tipPath)
        if ($key) {
          try {
            $children = @($key.GetSubKeyNames())
            $allowed = @('Category')
            if ($HiddenProfile) { $allowed += 'LanguageProfile' }
            if ($key.ValueCount -ne 0 -or @($children | Where-Object { $_ -notin $allowed }).Count) {
              throw "Unexpected data in $hive/$view/$tipPath; cleanup refused."
            }
            if ($children -contains 'LanguageProfile') {
              $languages = $key.OpenSubKey('LanguageProfile')
              try {
                if ($languages.ValueCount -ne 0 -or
                    @($languages.GetSubKeyNames() | Where-Object { $_ -ine '0x00000804' }).Count) {
                  throw 'Unexpected language profile; cleanup refused.'
                }
                $hans = $languages.OpenSubKey('0x00000804')
                if ($hans) {
                  try {
                    if ($hans.ValueCount -ne 0 -or @($hans.GetSubKeyNames() | Where-Object {
                      $_ -ine '{A2F702D2-DD84-4C9E-9C18-66FEBA75C4E3}'
                    }).Count) { throw 'Unexpected profile identity; cleanup refused.' }
                  } finally { $hans.Dispose() }
                }
              } finally { $languages.Dispose() }
            }
          } finally { $key.Dispose() }
          $base.DeleteSubKeyTree($tipPath, $false)
        }
      } finally { $base.Dispose() }
    }
  }
}

$mutex = [Threading.Mutex]::new($false, 'Local\WeaselProbeKeyboardCategoryTest')
$owned = $false
$recording = $false
try {
  if ($Transcript) {
    Start-Transcript -LiteralPath (Join-Path $sourceRoot 'output\ProbeDiagnostics\KeyboardCategoryTest.log') -Force
    $recording = $true
  }
  try { $owned = $mutex.WaitOne(0) }
  catch [Threading.AbandonedMutexException] { $owned = $true }
  if (-not $owned) { throw 'Another keyboard category test is running.' }
  Assert-ProbeAbsent
  if (Get-Process ProbeActivation -ErrorAction SilentlyContinue) {
    throw 'Another diagnostic process is running.'
  }
  $before = Get-InputState
  $failures = 0
  $total = $Count + [int]$InputAfterCycles.IsPresent
  for ($iteration = 1; $iteration -le $total; $iteration++) {
    if ($iteration -gt $Count -and $failures) {
      Write-Output 'Interactive input skipped because automated checks failed.'
      break
    }
    $iterationMode = if ($iteration -gt $Count) { 'input' } else { $Mode }
    Write-Output "Keyboard category test $iteration/$total ($iterationMode)"
    try {
      $option = if ($HiddenProfile) { "--keyboard-hidden-$iterationMode" } else { "--keyboard-$iterationMode" }
      $childLog = Join-Path $sourceRoot 'output\ProbeDiagnostics\x64\FreshCategoryChild.log'
      if (Test-Path -LiteralPath $childLog) { Remove-Item -LiteralPath $childLog -Force }
      & $executable $dll $probeClsid $option
      $diagnosticExit = $LASTEXITCODE
      if (Test-Path -LiteralPath $childLog) {
        Copy-Item -LiteralPath $childLog -Destination (
          Join-Path $sourceRoot "output\ProbeDiagnostics\x64\Category-$iteration-$iterationMode.log") -Force
      }
      if ($diagnosticExit -ne 0) { $failures++ }
      Write-Output "Diagnostic exit = $diagnosticExit"
    } finally {
      Remove-TestCategoryRoots
      Assert-ProbeAbsent
      if ((Get-InputState) -cne $before) {
        throw 'Existing input method registrations or user list changed.'
      }
      Write-Output 'Probe category roots removed; existing COM paths and input list unchanged.'
    }
  }
  Write-Output "Keyboard category test failures = $failures/$Count"
  if ($failures) { exit 1 }
} finally {
  if ($owned) { $mutex.ReleaseMutex() }
  $mutex.Dispose()
  if ($recording) { Stop-Transcript }
}

param(
  [ValidateSet('Status', 'Register', 'Unregister')]
  [string]$Action = 'Status'
)

$ErrorActionPreference = 'Stop'
$originalClsid = '{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}'
$probeClsid = '{B749B45E-7DCE-4496-8414-9EE10970260A}'
$probeProfile = '{A2F702D2-DD84-4C9E-9C18-66FEBA75C4E3}'
$probeTip = "0804:$probeClsid$probeProfile"
$probe64 = Join-Path $PSScriptRoot 'output\Probe\v2\x64\weaselProbe.dll'
$probe32 = Join-Path $PSScriptRoot 'output\Probe\v2\Win32\weaselProbe.dll'

function Get-InprocPath {
  param(
    [Microsoft.Win32.RegistryHive]$Hive,
    [Microsoft.Win32.RegistryView]$View,
    [string]$Clsid
  )
  $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, $View)
  try {
    $prefix = if ($Hive -eq [Microsoft.Win32.RegistryHive]::CurrentUser) {
      'Software\Classes\CLSID'
    } else {
      'CLSID'
    }
    $key = $base.OpenSubKey("$prefix\$Clsid\InprocServer32")
    if ($null -eq $key) { return $null }
    try { return [string]$key.GetValue('') }
    finally { $key.Close() }
  }
  finally { $base.Close() }
}

function Get-TipPresent {
  $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::LocalMachine,
    [Microsoft.Win32.RegistryView]::Registry64)
  try {
    $key = $base.OpenSubKey(
      "SOFTWARE\Microsoft\CTF\TIP\$probeClsid\LanguageProfile\0x00000804\$probeProfile")
    if ($null -eq $key) { return $false }
    $key.Close()
    return $true
  }
  finally { $base.Close() }
}

function Test-InputMethodListed {
  foreach ($language in (Get-WinUserLanguageList)) {
    foreach ($tip in $language.InputMethodTips) {
      if ($tip -ieq $probeTip) { return $true }
    }
  }
  return $false
}

function Set-InputMethodListed {
  param([bool]$Listed)
  if (-not ('ProbeInputMethod' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class ProbeInputMethod {
  [DllImport("input.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool InstallLayoutOrTip(string profile, uint flags);
}
'@
  }
  $flags = if ($Listed) { [uint32]0 } else { [uint32]1 }
  if (-not [ProbeInputMethod]::InstallLayoutOrTip("0x$probeTip", $flags)) {
    throw "InstallLayoutOrTip failed for $probeTip (flags $flags)."
  }
}

function Get-State {
  return [pscustomobject]@{
    Original64 = Get-InprocPath ClassesRoot Registry64 $originalClsid
    Original32 = Get-InprocPath ClassesRoot Registry32 $originalClsid
    Probe64 = Get-InprocPath ClassesRoot Registry64 $probeClsid
    Probe32 = Get-InprocPath ClassesRoot Registry32 $probeClsid
    ProbeTip = Get-TipPresent
    InputMethodListed = Test-InputMethodListed
  }
}

function Assert-Elevated {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Register/Unregister requires an elevated PowerShell session for TSF profile registration.'
  }
}

function Invoke-Regsvr32 {
  param([string]$Executable, [string]$Dll, [switch]$Unregister)
  $switches = if ($Unregister) { '/u /s' } else { '/s' }
  $process = Start-Process -FilePath $Executable -ArgumentList "$switches `"$Dll`"" -Wait -PassThru -WindowStyle Hidden
  if ($process.ExitCode -ne 0) {
    throw "regsvr32 failed for $Dll (exit code $($process.ExitCode))."
  }
}

$before = Get-State
if ($Action -eq 'Status') {
  $before | Format-List
  exit 0
}

if ($Action -eq 'Register') {
  throw 'The isolated probe failed real input testing (no Chinese input or language bar). Registration is disabled until activation and input are independently verified.'
}

if (-not $before.Probe64 -and -not $before.Probe32 -and
    -not $before.ProbeTip -and -not $before.InputMethodListed -and
    -not (Test-Path -LiteralPath "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\$probeClsid")) {
  $before | Format-List
  exit 0
}

Assert-Elevated
foreach ($item in @(@($before.Probe64, $probe64), @($before.Probe32, $probe32))) {
  if ($item[0] -and $item[0] -ine $item[1]) {
    throw "The probe CLSID already points to another DLL: $($item[0])"
  }
}

$reg64 = 'C:\Windows\System32\regsvr32.exe'
$reg32 = 'C:\Windows\SysWOW64\regsvr32.exe'

if ($Action -eq 'Register') {
  foreach ($dll in @($probe64, $probe32)) {
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
      throw "Probe DLL missing: $dll"
    }
  }
  try {
    Invoke-Regsvr32 $reg32 $probe32
    Invoke-Regsvr32 $reg64 $probe64
    Set-InputMethodListed $true
    $after = Get-State
    if ($after.Probe64 -ine $probe64 -or
        $after.Probe32 -ine $probe32 -or
        -not $after.ProbeTip -or
        -not $after.InputMethodListed) {
      throw 'Probe registration verification failed.'
    }
  }
  catch {
    if (Test-InputMethodListed) {
      try { Set-InputMethodListed $false }
      catch { Write-Warning $_.Exception.Message }
    }
    foreach ($entry in @(@($reg64, $probe64), @($reg32, $probe32))) {
      try { Invoke-Regsvr32 $entry[0] $entry[1] -Unregister }
      catch { Write-Warning $_.Exception.Message }
    }
    throw
  }
} else {
  if ($before.InputMethodListed) { Set-InputMethodListed $false }
  Invoke-Regsvr32 $reg64 $probe64 -Unregister
  Invoke-Regsvr32 $reg32 $probe32 -Unregister
  $probeTipRoot = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\$probeClsid"
  if (Test-Path -LiteralPath $probeTipRoot) {
    if (Get-TipPresent) {
      throw 'The probe language profile remains; refusing to remove the test TIP root.'
    }
    Remove-Item -LiteralPath $probeTipRoot -Recurse -Force
  }
  $after = Get-State
  if ($after.Probe64 -or $after.Probe32 -or $after.ProbeTip -or
      $after.InputMethodListed) {
    throw 'Probe registration remains after unregister.'
  }
}

if ($after.Original64 -ine $before.Original64 -or
    $after.Original32 -ine $before.Original32) {
  throw 'Original Weasel registration changed; inspect the system before further testing.'
}
$after | Format-List

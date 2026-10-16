param([switch]$SnapshotConfirmed)
. (Join-Path $PSScriptRoot 'Common.ps1')
$packageRoot = Split-Path -Parent $PSScriptRoot
$null = Assert-DisposableGuest $packageRoot
if (!$SnapshotConfirmed) { throw 'Create a restorable VM snapshot, then supply -SnapshotConfirmed.' }
if (!(Test-IsAdministrator)) { throw 'Run this script in an administrator PowerShell inside the VM.' }
if (Test-Path -LiteralPath 'C:\WeaseLCTest') { throw 'Test directory already exists. Restore the clean VM snapshot before retrying.' }
# Only a fresh guest may use the upstream identity. Never overwrite a preexisting
# Weasel installation, including COM registrations outside the installer key.
foreach ($view in @([Microsoft.Win32.RegistryView]::Registry32,[Microsoft.Win32.RegistryView]::Registry64)) {
  foreach ($hive in @([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryHive]::CurrentUser)) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,$view)
    try {
      foreach ($name in @('Software\Rime\Weasel','Software\Classes\CLSID\{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}','Software\Microsoft\CTF\TIP\{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}')) {
        $key=$base.OpenSubKey($name)
        if ($key) { $key.Dispose(); throw "Existing Weasel state: $hive $view $name. Restore a clean snapshot." }
      }
    } finally { $base.Dispose() }
  }
}
if (Get-Process -Name WeaselServer -ErrorAction SilentlyContinue) { throw 'Existing Weasel server found.' }
if ((Test-Path -LiteralPath "$env:windir\System32\weasel.dll") -or (Test-Path -LiteralPath "$env:windir\SysWOW64\weasel.dll")) { throw 'Existing system Weasel DLL found.' }
New-Item -ItemType Directory -Path 'C:\WeaseLCTest' | Out-Null
Copy-Item -Path (Join-Path $packageRoot 'app\*') -Destination 'C:\WeaseLCTest' -Recurse
$setup = 'C:\WeaseLCTest\WeaselSetup.exe'
$install = Start-Process -FilePath $setup -ArgumentList '/s' -WorkingDirectory 'C:\WeaseLCTest' -WindowStyle Hidden -PassThru -Wait
if ($install.ExitCode -ne 0) { throw "Registration failed ($($install.ExitCode)); stop and restore the snapshot." }
$setting = Start-Process -FilePath $setup -ArgumentList '/userdir:C:\WeaseLCTest-User' -WorkingDirectory 'C:\WeaseLCTest' -WindowStyle Hidden -PassThru -Wait
if ($setting.ExitCode -ne 0) { throw 'User-directory setup failed; restore snapshot.' }
# No Run entry, scheduled task, Windows service or automatic model startup.
Write-Output 'Installed into the disposable VM. Close this elevated shell.'
Write-Output 'In a normal PowerShell, run scripts\Start-TestApps.ps1 from this package.'
Write-Output 'Rollback: export synthetic test results, then restore the clean VM snapshot.'

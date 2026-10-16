Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-MachineFingerprint {
  $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryView]::Registry64)
  try { $key=$base.OpenSubKey('SOFTWARE\Microsoft\Cryptography');try{$machine=[string]$key.GetValue('MachineGuid')}finally{$key.Dispose()} }
  finally { $base.Dispose() }
  if (!$machine) { throw 'Machine identity unavailable.' }
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($machine)))).Replace('-','').ToLowerInvariant() }
  finally { $sha.Dispose() }
}
function Assert-PackageIntegrity([string]$Root) {
  $rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
  $manifest = Get-Content -LiteralPath (Join-Path $rootPath 'manifest.json') -Raw | ConvertFrom-Json
  if ($manifest.format -ne 1 -or $manifest.purpose -ne 'disposable-windows-vm-testing') { throw 'Invalid package manifest.' }
  $items=@(Get-ChildItem -LiteralPath $rootPath -Recurse -Force)
  if($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }){throw 'Package cannot contain reparse points.'}
  $listed=@{}
  foreach ($file in $manifest.files) {
    if ([IO.Path]::IsPathRooted($file.path) -or $file.path -match '(^|[\\/])\.\.([\\/]|$)' -or $file.path.Contains(':')) { throw 'Unsafe manifest path.' }
    $path = [IO.Path]::GetFullPath((Join-Path $rootPath $file.path))
    if (!$path.StartsWith($rootPath+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'File outside package.' }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256 -or (Get-Item -LiteralPath $path).Length -ne $file.bytes) { throw "Package file mismatch: $($file.path)" }
    if($listed.ContainsKey($path)){throw 'Duplicate manifest entry.'};$listed[$path]=$true
  }
  foreach($item in $items | Where-Object {!$_.PSIsContainer}){if($item.FullName -ne (Join-Path $rootPath 'manifest.json') -and !$listed.ContainsKey($item.FullName)){throw "Unlisted package file: $($item.Name)"}}
  return $manifest
}
function Assert-DisposableGuest([string]$Root) {
  $manifest = Assert-PackageIntegrity $Root
  if ((Get-MachineFingerprint) -eq $manifest.buildMachine) { throw 'Refusing deployment on the development computer.' }
  $bios = Get-ItemProperty -LiteralPath 'HKLM:\HARDWARE\DESCRIPTION\System\BIOS'
  $identity = "$($bios.SystemManufacturer) $($bios.SystemProductName)"
  if ($identity -notmatch '(?i)VMware|VirtualBox|Virtual Machine|QEMU|KVM|Xen|Parallels') { throw 'A recognized disposable Windows VM is required. No installation performed.' }
  if (![Environment]::Is64BitOperatingSystem -or [Environment]::OSVersion.Version.Build -lt 22000) { throw 'This package targets x64 Windows 11.' }
  return $manifest
}
function Test-IsAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  try { return ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
  finally { $identity.Dispose() }
}

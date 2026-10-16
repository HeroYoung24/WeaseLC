param([ValidateSet('Install','Unregister','Status')][string]$Action='Status',[switch]$ValidateRoundTrip)
. (Join-Path $PSScriptRoot 'Common.ps1')
$source=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$stage=Join-Path $source 'output\LocalTest\Deployment'
if($Action -eq 'Status'){
  Write-Output "Install directory: $script:TestRoot"
  Get-TestRegistration
  exit 0
}
if(!(Test-Admin)){throw 'This operation needs the Windows UAC administrator prompt.'}
$baseline=Get-DailyBaseline
try{
  if($Action -eq 'Unregister'){Remove-TestRegistration;exit 0}
  if((Test-Path -LiteralPath $script:TestRoot) -or @(Get-TestRegistration).Count){throw 'Local-test state already exists. Inspect/unregister it before a fresh install.'}
  $manifest=Get-Content (Join-Path $stage 'manifest.json') -Raw | ConvertFrom-Json
  if($manifest.format -ne 1 -or $manifest.purpose -ne 'weaselc-side-by-side-local-test'){throw 'Wrong manifest.'}
  $items=@(Get-ChildItem -LiteralPath (Join-Path $stage 'App') -Recurse -Force)
  if($items | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'No reparse points allowed.'}
  $listed=@{}
  foreach($file in $manifest.files){
    if($file.path -notmatch '^App\\' -or $file.path -match '(^|[\\/])\.\.([\\/]|$)|:' -or [IO.Path]::IsPathRooted($file.path)){throw 'Unsafe file path.'}
    $path=[IO.Path]::GetFullPath((Join-Path $stage $file.path))
    if(!$path.StartsWith($stage+'\App\',[StringComparison]::OrdinalIgnoreCase) -or $listed.ContainsKey($path)){throw 'Unsafe/duplicate entry.'}
    if((Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256){throw 'Build hash mismatch.'}
    $listed[$path]=$true
  }
  foreach($file in $items | Where-Object {!$_.PSIsContainer}){if(!$listed.ContainsKey($file.FullName)){throw 'Unlisted application file.'}}
  # Program Files inherits an administrator-owned ACL; normal users cannot
  # replace the registered in-process COM DLL or its worker executable.
  Copy-Item -LiteralPath (Join-Path $stage 'App') -Destination $script:TestRoot -Recurse
  $success=$false
  try{
    $copied=@(Get-ChildItem -LiteralPath $script:TestRoot -Recurse -Force)
    if($copied | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint}){throw 'Copied tree contains a reparse point.'}
    if(@($copied | Where-Object {!$_.PSIsContainer}).Count -ne @($manifest.files).Count){throw 'Copied file list changed.'}
    foreach($file in $manifest.files){
      $copy=Join-Path $script:TestRoot $file.path.Substring(4)
      if((Get-Item -LiteralPath $copy).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $file.sha256){throw 'Copied hash mismatch.'}
    }
    $baseline | Set-Content -LiteralPath (Join-Path $script:TestRoot 'daily-baseline.json') -Encoding UTF8
    Copy-Item -LiteralPath (Join-Path $stage 'manifest.json') -Destination $script:TestRoot
    Set-TestMachineRoot $false
    foreach($item in @(@('System32','weaselx64.dll'),@('SysWOW64','Win32\weasel.dll'))){
      $dll=Join-Path $script:TestRoot $item[1]
      $code=Invoke-Program (Join-Path $env:windir ($item[0]+'\regsvr32.exe')) ('/s "'+$dll+'"')
      if($code -ne 0){throw "Register failed: $($item[1]), exit $code"}
    }
    if($ValidateRoundTrip){
      Remove-TestRegistration
      if((Get-DailyBaseline) -ne $baseline){throw 'Baseline changed during unregister rehearsal.'}
      Write-Output 'Registration rollback rehearsal passed; reinstalling the same independent identity.'
      Set-TestMachineRoot $false
      foreach($item in @(@('System32','weaselx64.dll'),@('SysWOW64','Win32\weasel.dll'))){
        $dll=Join-Path $script:TestRoot $item[1]
        $code=Invoke-Program (Join-Path $env:windir ($item[0]+'\regsvr32.exe')) ('/s "'+$dll+'"')
        if($code -ne 0){throw 'Registration after rollback rehearsal failed.'}
      }
    }
    if((Get-DailyBaseline) -ne $baseline){throw 'Daily installation changed unexpectedly.'}
    $success=$true
    Write-Output 'Installed WeaseLC side by side. No default input method, Windows service or startup entry was changed.'
  }finally{if(!$success){Remove-TestRegistration}}
}finally{
  if((Get-DailyBaseline) -ne $baseline){throw 'Daily installation baseline mismatch; inspect immediately.'}
}

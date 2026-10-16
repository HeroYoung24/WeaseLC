Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$script:TestClsid='{893169EA-8480-4965-9F2F-695ED51E4B43}'
$script:TestProfile='{FAF832B8-6CDD-4F9A-993D-22B47672BA27}'
$script:TestRoot=Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'WeaseLC-LocalTest'
if(![Environment]::Is64BitProcess){throw 'Use 64-bit PowerShell.'}
function Test-Admin {
  $id=[Security.Principal.WindowsIdentity]::GetCurrent()
  try{return ([Security.Principal.WindowsPrincipal]::new($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)}finally{$id.Dispose()}
}
function Invoke-Program([string]$File,[string]$Arguments) {
  $info=New-Object Diagnostics.ProcessStartInfo
  $info.FileName=$File;$info.Arguments=$Arguments;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
  $process=[Diagnostics.Process]::Start($info)
  try{if(!$process.WaitForExit(30000)){$process.Kill();$process.WaitForExit();throw "Timed out: $File"};return $process.ExitCode}finally{$process.Dispose()}
}
function Get-TestRegistration {
  $result=@()
  foreach($view in @([Microsoft.Win32.RegistryView]::Registry32,[Microsoft.Win32.RegistryView]::Registry64)){
    foreach($hive in @([Microsoft.Win32.RegistryHive]::LocalMachine,[Microsoft.Win32.RegistryHive]::CurrentUser)){
      $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey($hive,$view)
      try{foreach($name in @("Software\Classes\CLSID\$script:TestClsid","Software\Microsoft\CTF\TIP\$script:TestClsid",'Software\Rime\WeaseLC-LocalTest')){
        $key=$base.OpenSubKey($name);if($key){$result+="$hive $view $name";$key.Dispose()}
      }}finally{$base.Dispose()}
    }
  }
  return $result
}
function Set-TestMachineRoot([bool]$Remove) {
  foreach($view in @([Microsoft.Win32.RegistryView]::Registry32,[Microsoft.Win32.RegistryView]::Registry64)){
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$view)
    try{if($Remove){$base.DeleteSubKeyTree('Software\Rime\WeaseLC-LocalTest',$false)}else{
      $key=$base.CreateSubKey('Software\Rime\WeaseLC-LocalTest')
      try{$key.SetValue('WeaselRoot',$script:TestRoot,[Microsoft.Win32.RegistryValueKind]::String)}finally{$key.Dispose()}
    }}finally{$base.Dispose()}
  }
}
function Enable-InstalledTestProfile {
  $checker=Join-Path $PSScriptRoot '..\..\output\LocalTest\x64\ProfileCheck.exe'
  if(!(Test-Path -LiteralPath $checker)){throw 'Build the x64 ProfileCheck before installation.'}
  if((Invoke-Program $checker '--enable-installed') -ne 0){throw 'Could not enable the independent installed profile.'}
  # Never use the writing process's cached GetProfile as the persistence gate.
  if((Invoke-Program $checker '') -ne 0){throw 'Fresh-process verification of installed profile failed.'}
}
function Remove-TestRegistration {
  Set-TestInputList $true
  # Only our two DLLs are unregistered; no ordinary Weasel setup/uninstaller.
  foreach($item in @(@('System32','weaselx64.dll'),@('SysWOW64','Win32\weasel.dll'))){
    $dll=Join-Path $script:TestRoot $item[1]
    if(Test-Path -LiteralPath $dll){$code=Invoke-Program (Join-Path $env:windir ($item[0]+'\regsvr32.exe')) ('/u /s "'+$dll+'"');Write-Output "Unregister $($item[1]): $code"}
  }
  Set-TestMachineRoot $true
  # UnregisterProfile can leave the per-user Enable override behind after the
  # machine profile has disappeared. This exact identity belongs to this test.
  foreach($view in @([Microsoft.Win32.RegistryView]::Registry32,[Microsoft.Win32.RegistryView]::Registry64)){
    $machine=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$view)
    try{foreach($name in @("Software\Classes\CLSID\$script:TestClsid","Software\Microsoft\CTF\TIP\$script:TestClsid")){
      $key=$machine.OpenSubKey($name);if($key){$key.Dispose();throw 'Machine test registration remains; per-user cleanup refused.'}
    }}finally{$machine.Dispose()}
    $user=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,$view)
    try{$user.DeleteSubKeyTree("Software\Microsoft\CTF\TIP\$script:TestClsid",$false)}finally{$user.Dispose()}
  }
  # The second architecture may find profiles already removed by the first.
  # Verify actual absence, not just regsvr32's exit status.
  $remaining=@(Get-TestRegistration | Where-Object {$_ -notmatch '^CurrentUser .*Software\\Rime\\WeaseLC-LocalTest$'})
  if($remaining.Count){throw ('Registration remains: '+($remaining -join '; '))}
}
function Set-TestInputList([bool]$Remove) {
  $checker=Join-Path $PSScriptRoot '..\..\output\LocalTest\x64\ProfileCheck.exe'
  if(!$Remove){
    if(!(Test-Path -LiteralPath $checker)){throw 'Build test/LocalDeployment/ProfileCheck.vcxproj x64 before enabling the test input method.'}
    if((Invoke-Program $checker '--enable') -ne 0){throw 'TSF did not confirm the current-user profile is enabled.'}
  }
  if($Remove -and (Test-Path -LiteralPath $checker)){
    # Best effort if profiles have already been removed by an earlier rollback.
    $null=Invoke-Program $checker '--disable'
  }
  if(!('WeaseLCInputList' -as [type])){
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WeaseLCInputList {
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryExW(string name,IntPtr reserved,uint flags);
  [DllImport("kernel32.dll",CharSet=CharSet.Ansi,ExactSpelling=true)] static extern IntPtr GetProcAddress(IntPtr module,string name);
  [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
  [UnmanagedFunctionPointer(CallingConvention.StdCall,CharSet=CharSet.Unicode)]
  [return:MarshalAs(UnmanagedType.Bool)] delegate bool InstallDelegate(string profile,uint flags);
  public static bool Set(bool remove) {
    IntPtr module=LoadLibraryExW("input.dll",IntPtr.Zero,0x800);
    if(module==IntPtr.Zero)throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
    try {
      IntPtr entry=GetProcAddress(module,"InstallLayoutOrTip");
      if(entry==IntPtr.Zero)throw new InvalidOperationException("InstallLayoutOrTip missing");
      var call=(InstallDelegate)Marshal.GetDelegateForFunctionPointer(entry,typeof(InstallDelegate));
      // No DEFPROFILE / DEFUSER / CLEANINSTALL flags.
      return call("0804:{893169EA-8480-4965-9F2F-695ED51E4B43}{FAF832B8-6CDD-4F9A-993D-22B47672BA27}",remove?1u:0u);
    } finally {FreeLibrary(module);}
  }
}
'@
  }
  if(![WeaseLCInputList]::Set($Remove) -and !$Remove){throw 'Could not add the independent input profile.'}
  if(!$Remove -and (Invoke-Program $checker '--enable') -ne 0){throw 'TSF runtime enable failed after adding the profile.'}
}
function Get-DailyBaseline {
  $state=[ordered]@{}
  foreach($dir in @('System32','SysWOW64')){
    $path=Join-Path $env:windir ($dir+'\weasel.dll')
    $state[$dir]=if(Test-Path -LiteralPath $path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}else{$null}
  }
  foreach($view in @([Microsoft.Win32.RegistryView]::Registry32,[Microsoft.Win32.RegistryView]::Registry64)){
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine,$view)
    try{foreach($name in @('Software\Rime\Weasel','Software\Classes\CLSID\{A3F4CDED-B1E9-41EE-9CA6-7B4D0DE6CB0A}\InprocServer32')){
      $key=$base.OpenSubKey($name);$values=[ordered]@{}
      if($key){try{foreach($value in $key.GetValueNames()){$values[$value]=$key.GetValue($value)}}finally{$key.Dispose()}}
      $state["$view $name"]=$values
    }}finally{$base.Dispose()}
  }
  return ($state | ConvertTo-Json -Depth 6 -Compress)
}

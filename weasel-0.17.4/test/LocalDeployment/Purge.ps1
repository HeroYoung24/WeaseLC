param()
. (Join-Path $PSScriptRoot 'Common.ps1')
if(!(Test-Admin)){throw 'Run this final uninstall through UAC after closing the test windows.'}
$source=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$resultPath=Join-Path $source 'output\LocalTest\purge-result.json'
$before=Get-DailyBaseline
$result=[ordered]@{registrationRemoved=$false;settingsRemoved=$false;removed=@();residual=@();dailyBaselineUnchanged=$false;error=''}
function Remove-ExactTestDirectory([string]$Base,[string]$Name) {
  if($Name -notin @('WeaseLC-LocalTest','rime.weaselc-localtest')){throw 'Unknown test directory name.'}
  $expected=[IO.Path]::GetFullPath((Join-Path $Base $Name)).TrimEnd('\')
  if(!(Test-Path -LiteralPath $expected)){return}
  $root=Get-Item -LiteralPath $expected -Force
  if(!$root.PSIsContainer -or ![string]::Equals($root.FullName.TrimEnd('\'),$expected,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected resolved directory.'}
  $children=@(Get-ChildItem -LiteralPath $expected -Recurse -Force)
  if(($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ($children | Where-Object {$_.Attributes -band [IO.FileAttributes]::ReparsePoint})){throw 'Refusing to follow reparse points during uninstall.'}
  foreach($child in $children){if(!$child.FullName.StartsWith($expected+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Child escaped test directory.'}}
  try{Remove-Item -LiteralPath $expected -Recurse -Force -ErrorAction Stop;$script:result.removed+=@($expected)}
  catch{
    if(Test-Path -LiteralPath $expected){
      $script:result.residual+=@(Get-ChildItem -LiteralPath $expected -Recurse -Force -File | ForEach-Object {$_.FullName})
      if(!$script:result.residual.Count){$script:result.residual+=@($expected)}
    }
  }
}
try{
  # Unregister only the new identity before touching files. Never kill host apps
  # or schedule a DLL for deletion at reboot; occupied files are reported.
  Remove-TestRegistration
  $result.registrationRemoved=$true
  foreach($view in @([Microsoft.Win32.RegistryView]::Registry32,[Microsoft.Win32.RegistryView]::Registry64)){
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,$view)
    try{$base.DeleteSubKeyTree('Software\Rime\WeaseLC-LocalTest',$false)}finally{$base.Dispose()}
  }
  if(@(Get-TestRegistration).Count){throw 'Test registry state remains; files preserved for inspection.'}
  $result.settingsRemoved=$true
  Remove-ExactTestDirectory ([Environment]::GetFolderPath('ProgramFiles')) 'WeaseLC-LocalTest'
  Remove-ExactTestDirectory ([Environment]::GetFolderPath('ApplicationData')) 'WeaseLC-LocalTest'
  Remove-ExactTestDirectory ([IO.Path]::GetTempPath()) 'rime.weaselc-localtest'
}catch{$result.error=$_.Exception.ToString()}
finally{
  $result.dailyBaselineUnchanged=(Get-DailyBaseline) -eq $before
  $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultPath -Encoding UTF8
}
if($result.error -or !$result.dailyBaselineUnchanged){throw 'Uninstall requires inspection; see output\LocalTest\purge-result.json.'}
if($result.residual.Count){Write-Output 'Registration removed. Some files remain occupied; see purge-result.json and close/restart their host before retrying.';exit 3}
Write-Output 'Independent test registration, program, settings, dictionary and logs removed. Original installation unchanged.'

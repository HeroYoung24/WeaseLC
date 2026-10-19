param([Parameter(Mandatory=$true)][string]$Patch,[switch]$ValidateOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$source=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$workspaceOutput=Join-Path $source 'output\LocalTest'
$patchRoot=[IO.Path]::GetFullPath($Patch).TrimEnd('\')
if(!$patchRoot.StartsWith($workspaceOutput+'\BrokerPatch-',[StringComparison]::OrdinalIgnoreCase) -or
   [IO.Path]::GetDirectoryName($patchRoot) -ne $workspaceOutput){throw 'Use a dedicated BrokerPatch-* directory in output/LocalTest.'}
foreach($tree in @($patchRoot,$script:TestRoot)){
  foreach($entry in @((Get-Item -LiteralPath $tree)) + @(Get-ChildItem -LiteralPath $tree -Recurse -Force)){
    if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse points are not allowed.'}
  }
}
$spec=Get-Content (Join-Path $patchRoot 'patch.json') -Raw | ConvertFrom-Json
if($spec.format -ne 1 -or $spec.purpose -ne 'weaselc-correction-broker-patch'){throw 'Wrong patch format.'}
$installedManifest=Join-Path $script:TestRoot 'manifest.json'
if((Get-FileHash -LiteralPath $installedManifest -Algorithm SHA256).Hash -ne $spec.baseManifestSha256){throw 'Installed manifest differs from the patch base.'}
$manifest=Get-Content $installedManifest -Raw | ConvertFrom-Json
if($manifest.format -ne 1 -or $manifest.purpose -ne 'weaselc-side-by-side-local-test'){throw 'Wrong installed product.'}
$allowed=@('WeaselDeployer.exe','Win32\WeaselDeployer.exe')
if(@($spec.files).Count -ne 2 -or @($spec.files.path | Sort-Object -Unique).Count -ne 2){throw 'Exactly two broker executables are required.'}
foreach($file in $spec.files){
  if($file.path -cnotin $allowed -or $file.sha256 -notmatch '^[A-Fa-f0-9]{64}$' -or $file.previousSha256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Unexpected replacement file.'}
  $new=Join-Path (Join-Path $patchRoot 'new') $file.path
  $old=Join-Path $script:TestRoot $file.path
  if((Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash -ne $file.previousSha256 -or
     (Get-FileHash -LiteralPath $new -Algorithm SHA256).Hash -ne $file.sha256 -or
     (Get-Item -LiteralPath $new).Length -ne $file.bytes){throw 'Replacement/base hash mismatch.'}
}
function Assert-InstalledManifest($Expected){
  $listed=@{}
  foreach($file in $Expected.files){
    if($file.path -notmatch '^App\\' -or $file.path -match '(^|[\\/])\.\.([\\/]|$)|:'){throw 'Unsafe manifest path.'}
    $path=[IO.Path]::GetFullPath((Join-Path $script:TestRoot $file.path.Substring(4)))
    if(!$path.StartsWith($script:TestRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or $listed.ContainsKey($path)){throw 'Unsafe/duplicate installed path.'}
    $listed[$path]=$true
    if((Get-Item -LiteralPath $path).Length -ne $file.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256){throw ('Installed hash mismatch: '+$file.path)}
  }
  $actual=@(Get-ChildItem -LiteralPath $script:TestRoot -Recurse -File | Where-Object {$_.FullName -ne $installedManifest -and $_.FullName -ne (Join-Path $script:TestRoot 'daily-baseline.json')})
  if($actual.Count -ne $listed.Count){throw 'Unexpected installed file count.'}
  foreach($file in $actual){if(!$listed.ContainsKey($file.FullName)){throw 'Unlisted installed file.'}}
}
Assert-InstalledManifest $manifest
$baseline=Get-DailyBaseline
if($baseline -ne (Get-Content (Join-Path $script:TestRoot 'daily-baseline.json') -Raw).Trim()){throw 'Original input method baseline changed before patch.'}
if($ValidateOnly){Write-Output 'Broker-only patch and complete installed base validated.';return}
if(!(Test-Admin)){throw 'Applying this patch requires the Windows UAC administrator prompt.'}
foreach($process in Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('WeaselServer.exe','WeaselDeployer.exe')}){
  if(!$process.ExecutablePath){throw 'Cannot identify a backend process; stop the independent backend first.'}
  if($process.ExecutablePath.StartsWith($script:TestRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Stop the independent test backend/broker before applying the patch.'}
}
$backup=Join-Path $patchRoot 'backup'
if(Test-Path -LiteralPath $backup){throw 'Patch already attempted; preserve its backups.'}
$null=New-Item -ItemType Directory -Path (Join-Path $backup 'Win32')
foreach($file in $spec.files){Copy-Item -LiteralPath (Join-Path $script:TestRoot $file.path) -Destination (Join-Path $backup $file.path)}
Copy-Item -LiteralPath $installedManifest -Destination (Join-Path $backup 'manifest.json')
# Verify backups before the first mutation; the loaded TSF DLLs are never touched.
if((Get-FileHash -LiteralPath (Join-Path $backup 'manifest.json') -Algorithm SHA256).Hash -ne $spec.baseManifestSha256){throw 'Manifest backup hash mismatch.'}
foreach($file in $spec.files){if((Get-FileHash -LiteralPath (Join-Path $backup $file.path) -Algorithm SHA256).Hash -ne $file.previousSha256){throw 'Backup hash mismatch.'}}
$applied=$false;$restored=$false;$failure=''
try{
  foreach($file in $spec.files){
    Copy-Item -LiteralPath (Join-Path (Join-Path $patchRoot 'new') $file.path) -Destination (Join-Path $script:TestRoot $file.path)
    $entry=@($manifest.files | Where-Object {$_.path -ceq ('App\'+$file.path)})
    if($entry.Count -ne 1){throw 'Replacement absent from installed manifest.'}
    $entry[0].sha256=$file.sha256;$entry[0].bytes=$file.bytes
  }
  Assert-InstalledManifest $manifest
  if((Get-DailyBaseline) -ne $baseline){throw 'Original input method baseline changed.'}
  $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $installedManifest -Encoding UTF8
  $applied=$true
}catch{
  $failure=$_.Exception.ToString()
  foreach($file in $spec.files){Copy-Item -LiteralPath (Join-Path $backup $file.path) -Destination (Join-Path $script:TestRoot $file.path)}
  Copy-Item -LiteralPath (Join-Path $backup 'manifest.json') -Destination $installedManifest
  Assert-InstalledManifest (Get-Content $installedManifest -Raw | ConvertFrom-Json)
  $restored=$true
  throw
}finally{
  @{utc=[DateTime]::UtcNow.ToString('o');applied=$applied;restored=$restored;error=$failure;files=@($spec.files.path);dailyBaselineUnchanged=((Get-DailyBaseline) -eq $baseline)} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $patchRoot 'result.json') -Encoding UTF8
}
Write-Output 'Updated only the two correction broker executables; input-method DLLs and registration are unchanged.'

#requires -Version 7.0
param([Parameter(Mandatory)][string]$Package)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$packageRoot=(Resolve-Path -LiteralPath $Package).Path
if ([IO.Path]::GetDirectoryName($packageRoot) -ne (Join-Path $source 'output\Distribution')) { throw 'Use a package generated in output/Distribution.' }
$installer=Join-Path $packageRoot 'WeaseLC-0.1.0-beta.1-x64-setup.exe'
$extract=Join-Path $packageRoot 'extracted'
if (Test-Path -LiteralPath $extract) { throw 'Preserve the earlier extraction.' }
& (Join-Path $source 'output\7z.exe') x $installer "-o$extract" -y > (Join-Path $packageRoot 'extract.log')
if ($LASTEXITCODE) { throw 'Installer extraction failed.' }
$manifest=Get-Content (Join-Path $packageRoot 'App\distribution-manifest.json') -Raw | ConvertFrom-Json
$modelTools = @('Start-LocalModel.ps1','Download-Model.ps1','Qwen-4B-LICENSE.txt','llama-LICENSE.txt','MODEL-ASSETS.md') |
  ForEach-Object { 'model-tools\' + $_ }
if (Compare-Object $modelTools @($manifest.files.path -like 'model-tools\*')) { throw 'Unexpected model tools payload.' }
if ($manifest.files.path -match '(^|\\)(pwsh\.exe|llama-server\.exe)$|\.gguf$') { throw 'An external model or PowerShell asset entered the payload.' }
foreach ($entry in $manifest.files) {
  if ($entry.path -match '["$\r\n]' -or $entry.path -match '(^|\\)\.\.(\\|$)' -or [IO.Path]::IsPathRooted($entry.path)) { throw 'Unsafe manifest path.' }
  $file=Join-Path $extract $entry.path
  if (!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -cne $entry.sha256) { throw "Extracted payload differs: $($entry.path)" }
}
$launcher=Join-Path $extract 'WeaseLC.exe'
$before=Test-Path 'HKCU:\Software\WeaseLC'
$process=Start-Process -FilePath $launcher -ArgumentList /identity -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(5000) -or $process.ExitCode -ne 0) { throw 'Launcher identity smoke check failed.' }
if ((Test-Path 'HKCU:\Software\WeaseLC') -ne $before) { throw 'Read-only launcher check touched settings.' }
foreach ($arch in 'x64','Win32') {
  $guard = if ($arch -eq 'x64') {$launcher} else {Join-Path $extract 'Win32\WeaseLC.exe'}
  $process=Start-Process -FilePath $guard -ArgumentList /guard-selftest -WindowStyle Hidden -PassThru
  if (!$process.WaitForExit(10000) -or $process.ExitCode -ne 0) { throw "Uninstall boundary checks failed for $arch." }
  $cases=@(
    @{root=(Join-Path ([IO.Path]::GetPathRoot($env:windir)) ('WeaseLC-boundary-'+[Guid]::NewGuid().ToString('N')));exit=0},
    @{root=([IO.Path]::GetPathRoot($source));exit=1},
    @{root=$env:windir;exit=1},
    @{root=[Environment]::GetFolderPath('ProgramFiles');exit=1},
    @{root=[Environment]::GetFolderPath('ProgramFilesX86');exit=1},
    @{root=(Join-Path $env:windir '..\WeaseLC-safe-boundary-test');exit=1},
    @{root=(Join-Path $env:ProgramData 'WeaseLC-safe-boundary-test');exit=1}
  )
  foreach($case in $cases) {
    $info=[Diagnostics.ProcessStartInfo]::new($guard,'/guard-destination')
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.Environment['WEASELC_INSTALL_ROOT']=$case.root
    $check=[Diagnostics.Process]::Start($info)
    try {if(!$check.WaitForExit(10000) -or $check.ExitCode -ne $case.exit){throw "Destination safety check failed for $arch."}}
    finally {$check.Dispose()}
  }
  $directory = New-Item -ItemType Directory (Join-Path $packageRoot "ui-checks\$arch") -Force
  $deployer = if ($arch -eq 'x64') {Join-Path $extract 'WeaselDeployer.exe'} else {Join-Path $extract 'Win32\WeaselDeployer.exe'}
  $process = Start-Process -FilePath $deployer -ArgumentList /correction-selftest -WorkingDirectory $directory.FullName -WindowStyle Hidden -PassThru
  if (!$process.WaitForExit(60000) -or $process.ExitCode -ne 0) { throw "Packaged UI checks failed for $arch." }
  $report = Get-Content (Join-Path $directory.FullName CorrectionDialogChecks.log)
  if ($report -match ' = 0$') { throw "Packaged UI assertion failed for $arch." }
}
$result=[ordered]@{payloadFiles=$manifest.files.Count;allExtractedHashesMatch=$true;readOnlyLauncherPassed=$true;externalModelAndShellExcluded=$true;
  packagedUiPassed=$true;uninstallBoundaryPassed=$true;sourceCommit=$manifest.sourceCommit;dirtySource=$manifest.dirtySource;installed=$false}
$result | ConvertTo-Json | Set-Content (Join-Path $packageRoot 'package-check.json') -Encoding utf8
$result | ConvertTo-Json

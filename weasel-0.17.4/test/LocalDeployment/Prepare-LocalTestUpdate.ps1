#requires -Version 7.0
param()
. (Join-Path $PSScriptRoot 'Common.ps1')
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$destination = Join-Path $source ('output\LocalTest\UpdatePatch-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if (Test-Path -LiteralPath $destination) { throw 'Preserve the earlier patch.' }
$manifestPath = Join-Path $script:TestRoot 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.purpose -ne 'weaselc-side-by-side-local-test' -or $manifest.format -ne 1) { throw 'Wrong installed product.' }
$files = @()
foreach ($architecture in @('x64','Win32')) {
  foreach ($name in @('WeaselServer.exe','WeaselDeployer.exe',$(if ($architecture -eq 'x64') {'weaselx64.dll'} else {'weasel.dll'}))) {
    $relative = if ($architecture -eq 'x64') {$name} else {'Win32\'+$name}
    $binary = Join-Path $source ('output\LocalTest\'+$architecture+'\'+$name)
    $oldEntry = @($manifest.files | Where-Object {$_.path -ceq ('App\'+$relative)})
    if ($oldEntry.Count -ne 1) { throw 'Expected binary absent from installed manifest.' }
    $files += @{path=$relative;sha256=(Get-FileHash -LiteralPath $binary).Hash;bytes=(Get-Item -LiteralPath $binary).Length;
                previousSha256=$oldEntry[0].sha256;source=$binary}
  }
}
$sourceCommit = (& git -C $source rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $sourceCommit -notmatch '^[a-f0-9]{40}$') { throw 'Cannot identify source revision.' }
$null = New-Item -ItemType Directory -Path (Join-Path $destination 'new\Win32')
foreach ($file in $files) { Copy-Item -LiteralPath $file.source -Destination (Join-Path (Join-Path $destination 'new') $file.path) }
@{format=1;purpose='weaselc-six-file-update';sourceCommit=$sourceCommit;
  baseManifestSha256=(Get-FileHash -LiteralPath $manifestPath).Hash;
  files=@($files | ForEach-Object {@{path=$_.path;sha256=$_.sha256;bytes=$_.bytes;previousSha256=$_.previousSha256}})} |
  ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'patch.json') -Encoding UTF8
& (Join-Path $PSScriptRoot 'Update-LocalTest.ps1') -Patch $destination -ValidateOnly
Write-Output $destination

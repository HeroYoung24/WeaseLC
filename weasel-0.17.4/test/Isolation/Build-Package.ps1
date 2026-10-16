param([string]$Git = 'git', [ValidatePattern('^[a-zA-Z0-9-]{1,40}$')][string]$Label = 'first')
. (Join-Path $PSScriptRoot 'Common.ps1')
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$repositoryRoot = Split-Path -Parent $sourceRoot
$head = & $Git -C $repositoryRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit.' }
$dirty = & $Git -C $repositoryRoot status --porcelain --untracked-files=normal
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Commit reviewed source changes before packaging.' }
$name = 'WeaseLC-isolation-'+$head.Substring(0,8)+'-'+$Label
$out = Join-Path $sourceRoot 'output\IsolationTest'
$bundle = Join-Path $out $name
if ((Test-Path -LiteralPath $bundle) -or (Test-Path -LiteralPath ($bundle+'.zip'))) { throw 'Package already exists; choose a new source commit rather than overwriting it.' }
New-Item -ItemType Directory -Path $bundle -Force | Out-Null
function Copy-Payload([string]$Relative, [string]$Destination) {
  $from=Join-Path $sourceRoot $Relative
  if(!(Test-Path -LiteralPath $from -PathType Leaf)){throw "Missing payload: $Relative. Build both architectures and diagnostics first."}
  $to=Join-Path $bundle $Destination
  New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
  Copy-Item -LiteralPath $from -Destination $to
}
foreach($file in @('weasel.dll','weaselx64.dll','WeaselSetup.exe','WeaselServer.exe','WeaselDeployer.exe','rime.dll')){
  Copy-Payload "output\$file" "app\$file"
}
foreach($file in @('WeaselServer.exe','WeaselDeployer.exe','rime.dll')){Copy-Payload "output\Win32\$file" "app\Win32\$file"}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'output\data') -Destination (Join-Path $bundle 'app\data') -Recurse
foreach($arch in @('x64','Win32')){
  foreach($item in @(@('SecurityChecks','SecurityChecks.exe'),@('CorrectionTests','CorrectionClient.exe'),@('CorrectionIntegration','CorrectionIntegration.exe'),@('ProbeDiagnostics','ProbeActivation.exe'))){
    Copy-Payload "output\$($item[0])\$arch\$($item[1])" "diagnostics\$arch\$($item[1])"
  }
  Copy-Payload "output\Probe\v3\$arch\weaselProbe.dll" "diagnostics\$arch\weaselProbe.dll"
}
foreach($file in @('Common.ps1','SmokeTest.ps1','Install-InDisposableVM.ps1','Start-TestApps.ps1','Start-SyntheticModel.ps1','SyntheticResponse.ps1')){
  Copy-Payload "test\Isolation\$file" "scripts\$file"
}
foreach($file in @('README.md','APPLICATION_MATRIX.md')){Copy-Payload "test\Isolation\$file" $file}
foreach($file in @('LICENSE.txt','librime\LICENSE','librime\deps\opencc\LICENSE','deps\boost_1_84_0\LICENSE_1_0.txt')){
  Copy-Payload $file ('licenses\'+$file.Replace('\','-'))
}
Copy-Payload 'docs\BUILD_BASELINE.md' 'licenses\DEPENDENCIES.md'
# Do not copy the old online installer/updater, curl, 7-Zip, batch installers,
# real settings/dictionaries, machine paths, keys, model weights or PDBs.
& $Git -C $repositoryRoot archive --format=zip "--output=$(Join-Path $bundle 'source.zip')" HEAD
if($LASTEXITCODE -ne 0){throw 'Source archive failed.'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$sourceZip=[IO.Compression.ZipFile]::Open((Join-Path $bundle 'source.zip'),[IO.Compression.ZipArchiveMode]::Update)
try {
  foreach($entry in @($sourceZip.Entries | Where-Object {$_.FullName -match '(?i)^weasel-0\.17\.4/output/(7z|curl)\.exe$'})){$entry.Delete()}
} finally {$sourceZip.Dispose()}
'Source archive omits the two obsolete upstream prebuilt output/7z.exe and output/curl.exe. Source code and submodule pins are unchanged.' |
  Set-Content -LiteralPath (Join-Path $bundle 'SOURCE_OMISSIONS.txt') -Encoding UTF8
& $Git -C $repositoryRoot submodule status | Set-Content -LiteralPath (Join-Path $bundle 'SUBMODULES.txt') -Encoding UTF8
# Verify PE bitness without loading/executing payloads.
$pe=@{}
foreach($file in Get-ChildItem -LiteralPath $bundle -Recurse -File | Where-Object Extension -In '.exe','.dll'){
  $data=[IO.File]::ReadAllBytes($file.FullName)
  if($data.Length -lt 64 -or [BitConverter]::ToUInt16($data,0) -ne 0x5a4d){throw 'Invalid DOS header.'}
  $offset=[BitConverter]::ToInt32($data,60)
  if($offset -lt 0 -or $offset -gt $data.Length-24 -or [BitConverter]::ToUInt32($data,$offset) -ne 0x4550){throw 'Invalid PE header.'}
  $machine=[BitConverter]::ToUInt16($data,$offset+4)
  $relative=$file.FullName.Substring($bundle.Length+1)
  $expected=if($relative -match 'Win32[\\/]|app[\\/](weasel\.dll|WeaselSetup\.exe)$'){0x14c}else{0x8664}
  if($machine -ne $expected){throw "Wrong architecture: $relative"}
  $pe[$relative]=if($machine -eq 0x8664){'x64'}else{'x86'}
}
$files=@(Get-ChildItem -LiteralPath $bundle -Recurse -File | Sort-Object FullName | ForEach-Object {
  @{path=$_.FullName.Substring($bundle.Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
@{format=1;purpose='disposable-windows-vm-testing';sourceCommit=$head;buildMachine=(Get-MachineFingerprint);createdUtc=[DateTime]::UtcNow.ToString('o');files=$files;pe=$pe;installedApplicationTestPassed=$false} |
  ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Encoding UTF8
$null = Assert-PackageIntegrity $bundle
Compress-Archive -LiteralPath $bundle -DestinationPath ($bundle+'.zip') -CompressionLevel Optimal
$hash=Get-FileHash -LiteralPath ($bundle+'.zip') -Algorithm SHA256
($hash.Hash.ToLowerInvariant()+'  '+$name+'.zip') | Set-Content -LiteralPath ($bundle+'.zip.sha256') -Encoding ASCII
Write-Output "Package: $bundle.zip"
Write-Output "SHA256: $($hash.Hash)"
Write-Output 'NOT INSTALLED OR ACCEPTED IN REAL APPS. The installation script rejects the build computer.'

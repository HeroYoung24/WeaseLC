#requires -Version 7.0
param([string]$MSBuild, [string]$NSISRoot, [switch]$AllowDirty)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$commit = (& git -C $source rev-parse HEAD).Trim()
if ($LASTEXITCODE) { throw 'Cannot identify source commit.' }
$dirty = !!(& git -C $source status --porcelain)
if ($dirty -and !$AllowDirty) { throw 'Commit reviewed changes first, or use -AllowDirty for a local preview.' }
if (!$MSBuild) {
  $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
  $MSBuild = (& $vswhere -latest -products '*' -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1)
}
if (!$MSBuild -or !(Test-Path -LiteralPath $MSBuild)) { throw 'Supply -MSBuild.' }
$vcTargets = Join-Path (Split-Path (Split-Path (Split-Path $MSBuild))) 'Microsoft\VC\v170\'
if (!(Test-Path -LiteralPath $vcTargets)) { throw 'Install the v143 MSBuild integration.' }
if (!$NSISRoot) { $NSISRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\NSIS' -ErrorAction SilentlyContinue).'(default)' }
$compiler = Join-Path $NSISRoot 'Bin\makensis.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw 'Supply -NSISRoot.' }
$root = Join-Path $source 'output\Distribution'
$logs = New-Item -ItemType Directory -Force (Join-Path $root 'logs')
Push-Location $source
try {
  foreach ($arch in 'x64','Win32') {
    $common = @('/p:Configuration=Release',"/p:Platform=$arch",'/p:WEASELC_DISTRIBUTION_BUILD=1',
      '/p:WEASELC_LOCAL_TEST_BUILD=0','/p:WEASEL_CORRECTION_PROBE_BUILD=0',"/p:VCTargetsPath=$vcTargets",'/m:1','/nr:false','/v:minimal')
    & $MSBuild weasel.sln /t:WeaselTSF,WeaselServer,WeaselDeployer @common > (Join-Path $logs "$arch-build.log")
    if ($LASTEXITCODE) { throw "Product build failed for $arch. See $logs." }
    & $MSBuild packaging/windows/WeaseLC.vcxproj @common > (Join-Path $logs "$arch-launcher.log")
    if ($LASTEXITCODE) { throw "Launcher build failed for $arch. See $logs." }
  }
} finally { Pop-Location }
$stage = Join-Path $root ('Package-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
if (Test-Path -LiteralPath $stage) { throw 'Preserve the earlier package.' }
$payload = New-Item -ItemType Directory (Join-Path $stage 'App')
$null = New-Item -ItemType Directory (Join-Path $payload 'Win32')
$licenses = New-Item -ItemType Directory (Join-Path $payload 'licenses')
foreach ($arch in 'x64','Win32') {
  $destination = if ($arch -eq 'x64') {$payload.FullName} else {Join-Path $payload 'Win32'}
  foreach ($name in 'WeaselServer.exe','WeaselDeployer.exe',$(if ($arch -eq 'x64') {'weaselx64.dll'} else {'weasel.dll'})) {
    $binary = Join-Path $root "$arch\$name"
    $bytes = [IO.File]::ReadAllBytes($binary)
    if ($bytes.Length -lt 256 -or [BitConverter]::ToUInt16($bytes,0) -ne 0x5a4d) { throw 'Invalid binary.' }
    $machine = [BitConverter]::ToUInt16($bytes,[BitConverter]::ToInt32($bytes,0x3c)+4)
    if ($machine -ne $(if($arch -eq 'x64'){0x8664}else{0x14c})) { throw 'Architecture mismatch.' }
    if ($name -like 'weasel*.dll') {
      $encoded = [Text.Encoding]::Unicode.GetString($bytes)
      if (!$encoded.Contains('WeaseLC-Pipe') -or $encoded.Contains('WeaseLC-LocalTest-Pipe')) { throw 'Wrong TSF identity.' }
      $hex = [Convert]::ToHexString($bytes)
      foreach ($id in '{69C4CD90-CA76-4367-A307-EE18A40EE6D9}','{7DB86CC4-CCAA-43FC-8601-85001F2AD0E0}') {
        if (!$hex.Contains([Convert]::ToHexString(([guid]$id).ToByteArray()))) { throw 'Distribution profile GUID absent.' }
      }
    }
    Copy-Item -LiteralPath $binary -Destination $destination
  }
  $rime = if ($arch -eq 'x64') {'output\rime.dll'} else {'output\Win32\rime.dll'}
  Copy-Item -LiteralPath (Join-Path $source $rime) -Destination $destination
  Copy-Item -LiteralPath (Join-Path $source 'output\data') -Destination (Join-Path $destination 'data') -Recurse
}
Copy-Item -LiteralPath (Join-Path $root 'x64\WeaseLC.exe') -Destination $payload
Copy-Item -LiteralPath (Join-Path $source 'LICENSE.txt') -Destination $payload
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'INSTALL-NOTES.txt') -Destination $payload
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.md') -Destination $payload
$licenseFiles = [ordered]@{
  'librime.txt'='librime\LICENSE'; 'boost.txt'='deps\boost_1_84_0\LICENSE_1_0.txt'
  'glog.txt'='librime\deps\glog\COPYING'; 'leveldb.txt'='librime\deps\leveldb\LICENSE'
  'yaml-cpp.txt'='librime\deps\yaml-cpp\LICENSE'; 'marisa-trie.txt'='librime\deps\marisa-trie\COPYING.md'
  'opencc.txt'='librime\deps\opencc\LICENSE'; 'WTL-MS-PL.txt'='packaging\windows\licenses\WTL-MS-PL.txt'
}
foreach ($entry in $licenseFiles.GetEnumerator()) {
  Copy-Item -LiteralPath (Join-Path $source $entry.Value) -Destination (Join-Path $licenses $entry.Key)
}
Copy-Item -LiteralPath (Join-Path $NSISRoot COPYING) -Destination (Join-Path $licenses 'NSIS-COPYING.txt')
foreach ($package in Get-ChildItem (Join-Path $source 'plum\package\rime') -Directory) {
  Copy-Item -LiteralPath (Join-Path $package.FullName LICENSE) -Destination (Join-Path $licenses ("data-"+$package.Name+".txt"))
}
$files = @(Get-ChildItem $payload -Recurse -File | Sort-Object FullName | ForEach-Object {
  if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not distributable inputs.' }
  $relative = $_.FullName.Substring($payload.FullName.Length+1)
  if ($relative -match '["$\r\n]' -or $relative -match '(^|\\)\.\.(\\|$)') { throw 'Unsafe payload filename.' }
  @{path=$relative;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;bytes=$_.Length}
})
@{format=1;purpose='weaselc-distribution';version='0.1.0-beta.1';fileVersion='0.1.0.1';
  sourceCommit=$commit;dirtySource=$dirty;
  clsid='{69C4CD90-CA76-4367-A307-EE18A40EE6D9}';profile='{7DB86CC4-CCAA-43FC-8601-85001F2AD0E0}';files=$files} | ConvertTo-Json -Depth 6 |
  Set-Content -LiteralPath (Join-Path $payload 'distribution-manifest.json') -Encoding utf8
$paths = @($files.path) + 'distribution-manifest.json'
$include = [Collections.Generic.List[string]]::new()
$include.Add('!macro InstallPayload')
foreach ($file in $paths) {
  $parent = Split-Path $file
  $include.Add(('  SetOutPath "$INSTDIR' + $(if($parent){'\'+$parent}else{''}) + '"'))
  $include.Add(('  File "' + (Join-Path $payload $file) + '"'))
}
$include.Add('!macroend')
$include.Add('!macro RemovePayload')
foreach ($file in $paths) { $include.Add('  Delete /REBOOTOK "$INSTDIR\' + $file + '"') }
$directories = @(Get-ChildItem $payload -Recurse -Directory | Sort-Object {$_.FullName.Length} -Descending)
foreach ($dir in $directories) { $include.Add('  RMDir "$INSTDIR\' + $dir.FullName.Substring($payload.FullName.Length+1) + '"') }
$include.Add('!macroend')
$includePath = Join-Path $stage payload.nsh
$include | Set-Content -LiteralPath $includePath -Encoding utf8
$installer = Join-Path $stage 'WeaseLC-0.1.0-beta.1-x64-setup.exe'
& $compiler /V3 /INPUTCHARSET UTF8 "/DPAYLOAD_INCLUDE=$includePath" /DDISPLAY_VERSION=0.1.0-beta.1 "/DINSTALLER_OUTPUT=$installer" (Join-Path $PSScriptRoot installer.nsi) > (Join-Path $stage nsis.log)
if ($LASTEXITCODE) { Get-Content (Join-Path $stage nsis.log) -Tail 20; throw 'NSIS build failed.' }
((Get-FileHash -LiteralPath $installer).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($installer)) |
  Set-Content -LiteralPath (Join-Path $stage SHA256SUMS.txt) -Encoding ascii
Write-Output $installer

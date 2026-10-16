param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$source=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$stage=Join-Path $source 'output\LocalTest\Deployment'
if(Test-Path -LiteralPath $stage){throw 'Deployment staging already exists. Inspect it before creating another package.'}
$null=New-Item -ItemType Directory -Path (Join-Path $stage 'App\Win32') -Force
foreach($arch in @('x64','Win32')){
  $dest=if($arch -eq 'x64'){Join-Path $stage 'App'}else{Join-Path $stage 'App\Win32'}
  foreach($name in @('WeaselServer.exe','WeaselDeployer.exe')){
    Copy-Item -LiteralPath (Join-Path $source "output\LocalTest\$arch\$name") -Destination $dest
  }
  $dll=if($arch -eq 'x64'){'weaselx64.dll'}else{'weasel.dll'}
  Copy-Item -LiteralPath (Join-Path $source "output\LocalTest\$arch\$dll") -Destination $dest
  $rime=if($arch -eq 'x64'){'output\rime.dll'}else{'output\Win32\rime.dll'}
  Copy-Item -LiteralPath (Join-Path $source $rime) -Destination $dest
  Copy-Item -LiteralPath (Join-Path $source 'output\data') -Destination (Join-Path $dest 'data') -Recurse
}
$files=@(Get-ChildItem -LiteralPath (Join-Path $stage 'App') -Recurse -File | ForEach-Object {
  @{path=$_.FullName.Substring($stage.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash;bytes=$_.Length}
})
@{format=1;purpose='weaselc-side-by-side-local-test';files=$files} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $stage 'manifest.json') -Encoding UTF8
Write-Output $stage

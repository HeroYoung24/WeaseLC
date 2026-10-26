#requires -Version 7.0
# Deliberately scoped to a new blank Word process. No typing or document reads.
. (Join-Path $PSScriptRoot 'Common.ps1')
if (Test-Admin) { throw 'Launch Word as the ordinary desktop user.' }
if (@(Get-Process WINWORD -ErrorAction SilentlyContinue).Count) {
  throw 'Close Word yourself after saving your documents; an existing process must not receive this test.'
}
$source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$manifest = Get-Content -LiteralPath (Join-Path $script:TestRoot 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.purpose -ne 'weaselc-side-by-side-local-test' -or $manifest.format -ne 1) { throw 'Wrong installed product.' }
foreach ($name in @('weaselx64.dll','WeaselDeployer.exe','WeaselServer.exe')) {
  $installed = Join-Path $script:TestRoot $name
  $built = Join-Path $source ('output\LocalTest\x64\'+$name)
  $entry = @($manifest.files | Where-Object {$_.path -ceq ('App\'+$name)})
  if ($entry.Count -ne 1 -or (Get-FileHash -LiteralPath $installed).Hash -ne $entry[0].sha256 -or
      (Get-FileHash -LiteralPath $built).Hash -ne $entry[0].sha256) { throw 'Deploy the current matching diagnostic build before launching.' }
}
$key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Rime\WeaseLC-LocalTest\Correction')
if (!$key) { throw 'Independent test settings required.' }
try { $settings = [Text.Encoding]::UTF8.GetString($key.GetValue('SettingsV2')) | ConvertFrom-Json }
finally { $key.Dispose() }
if ($settings.mode -ne 0 -or $settings.automatic.enabled) { throw 'Word basic-input diagnostics require correction Off.' }
$candidates = @('C:\Program Files\Microsoft Office\root\Office16\WINWORD.EXE',
    'C:\Program Files (x86)\Microsoft Office\root\Office16\WINWORD.EXE')
$word = @($candidates | Where-Object {Test-Path -LiteralPath $_})
if ($word.Count -ne 1) { throw 'Cannot identify a unique Word installation.' }
$info = [Diagnostics.ProcessStartInfo]::new($word[0])
$info.UseShellExecute = $false
$info.Arguments = '/w /q'
$info.Environment['WEASELC_TRACE_KEYS'] = '1'
$process = [Diagnostics.Process]::Start($info)
try { Write-Output ('Blank Word diagnostic launch requested, PID '+$process.Id+'. No input sent; correction remains Off.') }
finally { $process.Dispose() }

param([string]$ReportDirectory = (Join-Path $env:TEMP ('WeaseLC-Smoke-'+[Guid]::NewGuid().ToString('N'))))
. (Join-Path $PSScriptRoot 'Common.ps1')
$packageRoot = Split-Path -Parent $PSScriptRoot
$null = Assert-PackageIntegrity $packageRoot
function Invoke-OwnedTest([string]$File, [string[]]$Arguments, [string]$Name) {
  # Start directly through .NET: Windows PowerShell 5.1 can lose ExitCode on
  # Start-Process objects, and native stderr is not a test failure by itself.
  $info=New-Object Diagnostics.ProcessStartInfo
  $info.FileName=$File
  $info.Arguments=($Arguments|ForEach-Object{'"'+$_+'"'}) -join ' '
  $info.WorkingDirectory=$ReportDirectory
  $info.UseShellExecute=$false;$info.CreateNoWindow=$true
  $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
  $process=[Diagnostics.Process]::Start($info)
  try {
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    if(!$process.WaitForExit(30000)){
      $process.Kill();$process.WaitForExit()
      throw "$Name exceeded its diagnostic deadline; its own process was stopped."
    }
    [IO.File]::WriteAllText((Join-Path $ReportDirectory ($Name+'.log')),$stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText((Join-Path $ReportDirectory ($Name+'.stderr.log')),$stderr.GetAwaiter().GetResult())
    if($process.ExitCode -ne 0){throw "$Name failed ($($process.ExitCode)); stop deployment and inspect logs."}
  } finally {$process.Dispose()}
}
if (Test-Path -LiteralPath $ReportDirectory) { throw 'Use a new report directory.' }
New-Item -ItemType Directory -Path $ReportDirectory | Out-Null
New-Item -ItemType Directory -Path (Join-Path $ReportDirectory 'output') | Out-Null
# The Unicode worker-path test writes beside its own diagnostic executable.
# Run copies so the verified input package remains unchanged and can be read-only.
Copy-Item -LiteralPath (Join-Path $packageRoot 'diagnostics') -Destination (Join-Path $ReportDirectory 'diagnostics') -Recurse
Push-Location -LiteralPath $ReportDirectory
try {
  foreach ($arch in @('x64','Win32')) {
    $folder = Join-Path $ReportDirectory "diagnostics\$arch"
    $dll = Join-Path $folder 'weaselProbe.dll'
    $cases = @(
      @{exe='SecurityChecks.exe';args=@();name='security'},
      @{exe='CorrectionClient.exe';args=@();name='client'},
      @{exe='CorrectionIntegration.exe';args=@($dll);name='commit'},
      @{exe='CorrectionIntegration.exe';args=@($dll,'--automatic');name='automatic'},
      @{exe='ProbeActivation.exe';args=@($dll,'{B749B45E-7DCE-4496-8414-9EE10970260A}','--load-only');name='load'},
      @{exe='ProbeActivation.exe';args=@($dll,'{B749B45E-7DCE-4496-8414-9EE10970260A}','--class-factory');name='factory'},
      @{exe='ProbeActivation.exe';args=@($dll,'{B749B45E-7DCE-4496-8414-9EE10970260A}','--composition-checks');name='composition'})
    foreach ($case in $cases) {
      $arguments = $case.args
      Invoke-OwnedTest (Join-Path $folder $case.exe) $arguments "$arch-$($case.name)"
    }
    Write-Output "PASS $arch independent host checks"
  }
  foreach($relative in @('app\WeaselDeployer.exe','app\Win32\WeaselDeployer.exe')){
    $architecture=if($relative -match 'Win32'){'Win32'}else{'x64'}
    Invoke-OwnedTest (Join-Path $packageRoot $relative) @('/correction-selftest') "$architecture-gui-process"
    Copy-Item -LiteralPath (Join-Path $ReportDirectory 'CorrectionDialogChecks.log') -Destination (Join-Path $ReportDirectory ($(if($relative -match 'Win32'){'Win32'}else{'x64'})+'-gui.log'))
  }
} finally { Pop-Location }
Write-Output "Reports: $ReportDirectory. This does not test installed input-method activation."

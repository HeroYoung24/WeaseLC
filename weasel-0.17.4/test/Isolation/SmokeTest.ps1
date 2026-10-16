param([string]$ReportDirectory = (Join-Path $env:TEMP ('WeaseLC-Smoke-'+[Guid]::NewGuid().ToString('N'))))
. (Join-Path $PSScriptRoot 'Common.ps1')
$packageRoot = Split-Path -Parent $PSScriptRoot
$null = Assert-PackageIntegrity $packageRoot
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
      & (Join-Path $folder $case.exe) @arguments *> (Join-Path $ReportDirectory "$arch-$($case.name).log")
      if ($LASTEXITCODE -ne 0) { throw "$arch $($case.name) failed. Stop deployment; see report." }
    }
    Write-Output "PASS $arch independent host checks"
  }
  foreach($relative in @('app\WeaselDeployer.exe','app\Win32\WeaselDeployer.exe')){
    $gui=Start-Process -FilePath (Join-Path $packageRoot $relative) -ArgumentList '/correction-selftest' -WorkingDirectory $ReportDirectory -WindowStyle Hidden -PassThru -Wait
    if($gui.ExitCode -ne 0){throw "GUI selftest failed: $relative"}
    Copy-Item -LiteralPath (Join-Path $ReportDirectory 'CorrectionDialogChecks.log') -Destination (Join-Path $ReportDirectory ($(if($relative -match 'Win32'){'Win32'}else{'x64'})+'-gui.log'))
  }
} finally { Pop-Location }
Write-Output "Reports: $ReportDirectory. This does not test installed input-method activation."

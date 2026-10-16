. (Join-Path $PSScriptRoot 'Common.ps1')
$packageRoot = Split-Path -Parent $PSScriptRoot
$null = Assert-DisposableGuest $packageRoot
if (Test-IsAdministrator) { throw 'Run the input method and test apps as a normal user, not as administrator.' }
if (!(Test-Path -LiteralPath 'C:\WeaseLCTest\WeaselServer.exe')) { throw 'Install the test package inside the VM first.' }
if (Get-Process -Name WeaselServer -ErrorAction SilentlyContinue) { throw 'A Weasel server is already running. Review it before starting another.' }
$deploy = Start-Process -FilePath 'C:\WeaseLCTest\WeaselDeployer.exe' -ArgumentList '/deploy' -WorkingDirectory 'C:\WeaseLCTest' -WindowStyle Hidden -PassThru -Wait
if ($deploy.ExitCode -ne 0) { throw "Rime schema deployment failed: $($deploy.ExitCode)." }
$server = Start-Process -FilePath 'C:\WeaseLCTest\WeaselServer.exe' -WorkingDirectory 'C:\WeaseLCTest' -WindowStyle Hidden -PassThru
Write-Output "Test server PID: $($server.Id). Verify Chinese input before enabling correction."
Write-Output 'Open Notepad manually; follow README and APPLICATION_MATRIX.md. No text is injected automatically.'

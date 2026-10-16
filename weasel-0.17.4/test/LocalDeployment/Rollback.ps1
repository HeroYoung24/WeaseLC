param()
. (Join-Path $PSScriptRoot 'Common.ps1')
if(Test-Admin){throw 'Run from a normal PowerShell; this script requests UAC only for unregistering its own COM identity.'}
Set-TestInputList $true
$server=Join-Path $script:TestRoot 'WeaselServer.exe'
if(Test-Path -LiteralPath $server){$null=Invoke-Program $server '/q'}
$runner=Join-Path $PSScriptRoot 'Run-Elevated.ps1'
$ps=Join-Path $env:windir 'System32\WindowsPowerShell\v1.0\powershell.exe'
$process=Start-Process -FilePath $ps -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File "'+$runner+'" -Action Unregister') -Verb RunAs -WindowStyle Hidden -PassThru
Write-Output "Unregister PID: $($process.Id). Result: output\LocalTest\elevated-Unregister.json"
Write-Output 'Existing app processes may retain the DLL until closed. Reboot if input mode is affected. Files and the independent test dictionary are retained for inspection; no delayed deletion is scheduled.'

param([switch]$Stop,[switch]$EnableInputMethod)
. (Join-Path $PSScriptRoot 'Common.ps1')
if(Test-Admin){throw 'Start the test backend as a normal user.'}
$server=Join-Path $script:TestRoot 'WeaselServer.exe'
if(!(Test-Path -LiteralPath $server)){throw 'Install the side-by-side build first.'}
if($Stop){$code=Invoke-Program $server '/q';if($code -ne 0){throw 'Test backend stop failed.'};exit 0}
$user=Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'WeaseLC-LocalTest'
if(!(Test-Path -LiteralPath $user)){
  $null=New-Item -ItemType Directory -Path $user
  # Fresh public schema only. Never copy the daily user's dictionaries/config.
  [IO.File]::WriteAllText((Join-Path $user 'default.custom.yaml'),"patch:`n  schema_list:`n    - schema: luna_pinyin_simp`n",[Text.UTF8Encoding]::new($false))
}
$code=Invoke-Program (Join-Path $script:TestRoot 'WeaselDeployer.exe') '/deploy'
if($code -ne 0){throw "Schema deployment failed: $code"}
$process=Start-Process -FilePath $server -WorkingDirectory $script:TestRoot -WindowStyle Hidden -PassThru
Write-Output "Local-test backend PID $($process.Id). Correction follows saved settings; new settings default to off."
if($EnableInputMethod){Set-TestInputList $false;Write-Output 'Added WeaseLC 本机测试 to the current user input-method list. The default remains unchanged.'}

param([Parameter(Mandatory=$true)][string]$Pythonw)
$ErrorActionPreference='Stop'
$source=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$directory=Join-Path $source ('output\BrokerTests\matrix-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $directory
$server=Join-Path $source 'output\BrokerTests\x64\BrokerCheck.exe'
$runner=Join-Path $PSScriptRoot 'RunClient.py'
$backend=Start-Process -FilePath $server -ArgumentList ('--serve "'+$directory+'"') -WindowStyle Hidden -PassThru
function Wait-File([string]$Path) {
  $deadline=[DateTime]::UtcNow.AddSeconds(15)
  while(!(Test-Path -LiteralPath $Path)) {
    if([DateTime]::UtcNow -gt $deadline){throw "Report did not appear: $Path"}
    Start-Sleep -Milliseconds 25
  }
}
$results=@()
try {
  Wait-File (Join-Path $directory 'ready.txt')
  Wait-File (Join-Path $directory 'root.txt')
  $root=Get-Content -LiteralPath (Join-Path $directory 'root.txt') -Raw
  foreach($mode in @('on','off')) {
    if($mode -eq 'off') {
      Set-Content -LiteralPath (Join-Path $directory 'disable') -Value 'disable'
      Wait-File (Join-Path $directory 'disabled.txt')
    }
    foreach($arch in @('x64','Win32')) {
      $client=Join-Path $source "output\BrokerTests\$arch\BrokerCheck.exe"
      foreach($context in @('normal','notepad')) {
        $report=Join-Path $directory "$mode-$arch-$context.json"
        $arguments='"'+$runner+'" --client "'+$client+'" --server "'+$server+'" --root "'+$root+'" --output "'+$report+'" --mode '+$mode
        if($context -eq 'notepad') {
          Invoke-CommandInDesktopPackage -PackageFamilyName Microsoft.WindowsNotepad_8wekyb3d8bbwe -AppId App -Command $Pythonw -Args $arguments -PreventBreakaway
        } else {
          $process=Start-Process -FilePath $Pythonw -ArgumentList $arguments -WindowStyle Hidden -PassThru
          if(!$process.WaitForExit(15000)){throw 'Synthetic client did not exit.'}
          if($process.ExitCode -ne 0){throw 'Synthetic normal client failed.'}
        }
        Wait-File $report
        # The writer closes the short report immediately; avoid a partial read.
        $result=$null
        for($attempt=0;$attempt -lt 20;$attempt++) {
          try{$candidate=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json;if($candidate){$result=$candidate;break}}catch{}
          Start-Sleep -Milliseconds 25
        }
        if(!$result -or !$result.configured -or !$result.checked -or !$result.metadataSent){throw "Synthetic chain failed: $report"}
        if($context -eq 'notepad' -and ($result.packageStatus -ne 122 -or $result.directSettingsError -ne 2)){throw 'Expected packaged registry-view contrast not reproduced.'}
        $results+=@([pscustomobject]@{mode=$mode;arch=$arch;context=$context;result=$result})
      }
    }
    $versions=@($results | Where-Object {$_.mode -eq $mode} | ForEach-Object {$_.result.revision} | Select-Object -Unique)
    if($versions.Count -ne 1){throw 'Settings revision differed between normal and packaged clients.'}
  }
} finally {
  Set-Content -LiteralPath (Join-Path $directory 'stop') -Value 'stop'
  if(!$backend.WaitForExit(15000)){throw 'Synthetic backend did not stop.'}
}
if($backend.ExitCode -ne 0){throw 'Backend did not confirm receipt of metadata.'}
$hostResult=Get-Content -LiteralPath (Join-Path $directory 'host-result.json') -Raw | ConvertFrom-Json
if(!$hostResult.metadataReceived -or $hostResult.modelCalls -ne 4){throw 'Expected four enabled model calls and zero disabled model calls.'}
$results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $directory 'matrix.json') -Encoding utf8
Write-Output "PASS: 8 normal/MSIX, x64/x86, on/off cases; metadata received; only four enabled requests invoked the fixture. Reports: $directory"

param([Parameter(Mandatory)][string]$Report)
$ErrorActionPreference = 'Stop'
$child = $null
try {
  $child = Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList @(
    '-NoProfile','-NonInteractive','-Command','[Threading.Thread]::Sleep(300000)') -WindowStyle Hidden -PassThru
  [IO.File]::WriteAllText($Report, [string]$child.Id)
  Add-Type -TypeDefinition @'
using System;
using System.Threading.Tasks;
public static class WeaseLCFixturePipe {
  public static Task Wait() { return Task.Run(() => { Console.In.ReadLine(); }); }
}
'@
  [WeaseLCFixturePipe]::Wait().Wait()
} catch {
  [IO.File]::WriteAllText($Report + '.error', [string]$_)
  throw
} finally {
  if ($child -and !$child.HasExited) { Stop-Process -InputObject $child -Force }
}

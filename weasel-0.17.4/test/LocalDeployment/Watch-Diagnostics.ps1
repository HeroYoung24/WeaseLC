param([ValidateRange(1,600)][int]$Seconds=180)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$source=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$directory=Join-Path $source 'output\LocalTest'
$null=New-Item -ItemType Directory -Path $directory -Force
$output=Join-Path $directory ('pipeline-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N')+'.jsonl')
$null=New-Item -ItemType File -Path $output
$path='Software\Rime\WeaseLC-LocalTest\Correction\Diagnostics'
function Read-Metadata($Key,[string]$Name,[int]$Bytes){
  if(!$Key){return $null}
  $value=$Key.GetValue($Name,$null)
  if($value -isnot [byte[]] -or $value.Length -ne $Bytes -or [BitConverter]::ToUInt32($value,0) -ne 1){return $null}
  return ,$value
}
function Get-Time([byte[]]$Value,[int]$Offset){
  try{return [DateTime]::FromFileTimeUtc([BitConverter]::ToInt64($Value,$Offset)).ToString('o')}catch{return $null}
}
$previous='';$events=0;$clock=[Diagnostics.Stopwatch]::StartNew()
Write-Output "Metadata only; leave the test editor in front. Output: $output"
while($clock.Elapsed.TotalSeconds -lt $Seconds){
  $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($path)
  $automatic=$null;$scope=$null;$capture=$null;$worker=$null;$broker=$null;$frontend=[ordered]@{};$scopeTrace=[ordered]@{};$keyDispatch=[ordered]@{}
  try{
    $a=Read-Metadata $key 'LastAutomatic' 24
    $s=Read-Metadata $key 'LastScope' 20
    $c=Read-Metadata $key 'LastCapture' 24
    $w=Read-Metadata $key 'LastWorker' 40
    $b=Read-Metadata $key 'LastBrokerCheck' 52
    if($a){$automatic=[ordered]@{stage=[BitConverter]::ToUInt32($a,4);focus=[BitConverter]::ToUInt32($a,8);hresult=('0x{0:X8}' -f [BitConverter]::ToUInt32($a,12));utc=Get-Time $a 16}}
    if($s){$scope=[ordered]@{stage=[BitConverter]::ToUInt32($s,4);hresult=('0x{0:X8}' -f [BitConverter]::ToUInt32($s,8));inputScope=[BitConverter]::ToInt32($s,12);variantType=[BitConverter]::ToUInt32($s,16)}}
    if($c){$capture=[ordered]@{totalUnits=[BitConverter]::ToUInt32($c,4);addedUnits=[BitConverter]::ToUInt32($c,8);retainedUnits=[BitConverter]::ToUInt32($c,12);utc=Get-Time $c 16}}
    if($w){$worker=[ordered]@{stage=[BitConverter]::ToUInt32($w,4);units=[BitConverter]::ToUInt32($w,8);provider=[BitConverter]::ToUInt32($w,12);state=[BitConverter]::ToUInt32($w,16);nativeError=[BitConverter]::ToUInt32($w,20);httpStatus=[BitConverter]::ToUInt32($w,24);elapsedMs=[BitConverter]::ToUInt32($w,28);utc=Get-Time $w 32}}
    if($b){$broker=[ordered]@{status=[BitConverter]::ToUInt32($b,4);units=[BitConverter]::ToUInt32($b,8);state=[BitConverter]::ToUInt32($b,12);nativeError=[BitConverter]::ToUInt32($b,16);httpStatus=[BitConverter]::ToUInt32($b,20);elapsedMs=[BitConverter]::ToUInt32($b,24);requestId=[BitConverter]::ToUInt64($b,28);settingsRevision=[BitConverter]::ToUInt64($b,36);utc=Get-Time $b 44}}
    foreach($name in @('LastKey','LastCommit','LastGate')){
      $v=Read-Metadata $key $name 32
      $frontend[$name]=$null
      if($v){$frontend[$name]=[ordered]@{stage=[BitConverter]::ToUInt32($v,4);units=[BitConverter]::ToUInt32($v,8);flags=[BitConverter]::ToUInt32($v,12);hresult=('0x{0:X8}' -f [BitConverter]::ToUInt32($v,16));source=[BitConverter]::ToUInt32($v,20);utc=Get-Time $v 24}}
    }
    foreach($name in @('LastScopeBeforeEnd','LastScopeAfterEnd','LastScopeReadEdit')) {
      $v=Read-Metadata $key $name 40;$scopeTrace[$name]=$null
      if($v) {
        $traceFlags=[BitConverter]::ToUInt32($v,8)
        $entry=[ordered]@{phase=[BitConverter]::ToUInt32($v,4);flags=$traceFlags;nativeFlags=(($traceFlags -shr 8) -band 255);nativeVerified=((($traceFlags -shr 8) -band 255) -eq 255)}
        $fields=@('rangeContext','selectionLock','original','start','end','selection','enumeration')
        for($i=0;$i -lt $fields.Count;$i++){$entry[$fields[$i]]=('0x{0:X8}' -f [BitConverter]::ToUInt32($v,12+4*$i))}
        $scopeTrace[$name]=$entry
      }
    }
    foreach($name in @('LastKeyDownDispatch','LastKeyUpDispatch','LastIntegratedDispatch')) {
      $v=Read-Metadata $key $name 40;$keyDispatch[$name]=$null
      if($v) {
        $entry=[ordered]@{}
        $fields=@('callbacks','dispatches','reuses','unhandled','actualMisses','reasons','invalidations')
        for($i=0;$i -lt $fields.Count;$i++){$entry[$fields[$i]]=[BitConverter]::ToUInt32($v,4+4*$i)}
        $entry.utc=Get-Time $v 32;$keyDispatch[$name]=$entry
      }
    }
  }finally{if($key){$key.Dispose()}}
  $state=[ordered]@{automatic=$automatic;scope=$scope;capture=$capture;worker=$worker;broker=$broker;frontend=$frontend;scopeTrace=$scopeTrace;keyDispatch=$keyDispatch}
  $signature=$state | ConvertTo-Json -Depth 5 -Compress
  if($signature -cne $previous){
    [ordered]@{observedUtc=[DateTime]::UtcNow.ToString('o');state=$state} | ConvertTo-Json -Depth 6 -Compress | Add-Content -LiteralPath $output -Encoding UTF8
    $previous=$signature;$events++
  }
  Start-Sleep -Milliseconds 100
}
Write-Output "Finished: $events metadata changes. $output"

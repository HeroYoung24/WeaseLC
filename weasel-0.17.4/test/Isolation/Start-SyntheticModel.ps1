param([ValidateRange(1024,65535)][int]$Port = 18964)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'SyntheticResponse.ps1')
$null = Assert-DisposableGuest (Split-Path -Parent $PSScriptRoot)
# Deliberately tiny, serial HTTP fixture for the first installed-app wiring test.
# No model, external HTTP, commands, user settings, logs or text persistence.
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,$Port)
$listener.Start()
Write-Output "SYNTHETIC FIXTURE ONLY: http://127.0.0.1:$Port/v1/chat/completions; model: weasel-synthetic"
Write-Output 'Only the exact compiled synthetic sentence can produce a suggestion. Ctrl+C stops this server.'
try {
  while ($true) {
    if(!$listener.Pending()){Start-Sleep -Milliseconds 100;continue}
    $connection=$listener.AcceptTcpClient()
    try {
      $connection.ReceiveTimeout=3000; $connection.SendTimeout=3000
      $stream=$connection.GetStream(); $bytes=[Collections.Generic.List[byte]]::new()
      $deadline=[DateTime]::UtcNow.AddSeconds(5)
      while ($true) {
        if ($bytes.Count -ge 8192 -or [DateTime]::UtcNow -gt $deadline) { throw 'Header bound.' }
        $next=$stream.ReadByte(); if($next -lt 0){throw 'Disconnected.'};$bytes.Add([byte]$next)
        $count=$bytes.Count
        if($count -ge 4 -and $bytes[$count-4] -eq 13 -and $bytes[$count-3] -eq 10 -and $bytes[$count-2] -eq 13 -and $bytes[$count-1] -eq 10){break}
      }
      $header=[Text.Encoding]::ASCII.GetString($bytes.ToArray())
      if($header -notmatch '^POST /v1/chat/completions HTTP/1\.[01]\r\n' -or $header -match '(?i)\r\nTransfer-Encoding:'){throw 'Unsupported request.'}
      $lengths=[regex]::Matches($header,'(?im)^Content-Length:\s*(\d+)\s*\r$')
      if($lengths.Count -ne 1){throw 'Expected one content length.'}
      $length=[int]$lengths[0].Groups[1].Value
      if($length -le 0 -or $length -gt 16384){throw 'Body bound.'}
      $body=New-Object byte[] $length; $offset=0
      while($offset -lt $length){if([DateTime]::UtcNow -gt $deadline){throw 'Deadline.'};$read=$stream.Read($body,$offset,$length-$offset);if(!$read){throw 'Disconnected.'};$offset+=$read}
      $payload=[Text.UTF8Encoding]::new($false,$true).GetString($body)|ConvertFrom-Json
      $response=New-SyntheticResponse $payload
      $encoded=[Text.Encoding]::UTF8.GetBytes($response)
      $head=[Text.Encoding]::ASCII.GetBytes("HTTP/1.1 200 OK`r`nContent-Type: application/json`r`nContent-Length: $($encoded.Length)`r`nConnection: close`r`n`r`n")
      $stream.Write($head,0,$head.Length);$stream.Write($encoded,0,$encoded.Length)
    } catch { Write-Output 'Rejected malformed or incomplete synthetic request.' }
    finally { $connection.Dispose() }
  }
} finally { $listener.Stop() }

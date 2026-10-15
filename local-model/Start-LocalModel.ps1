param([int]$Port = 18963, [ValidateSet('1.7b','4b')][string]$Variant = '4b')
$ErrorActionPreference = 'Stop'
if ($Port -lt 1024 -or $Port -gt 65535) { throw 'Use an unprivileged local port.' }
$name = if ($Variant -eq '4b') { 'Qwen3-4B-Q4_K_M.gguf' } else { 'Qwen3-1.7B-Q8_0.gguf' }
$hash = if ($Variant -eq '4b') { '7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5' } else { '061b54daade076b5d3362dac252678d17da8c68f07560be70818cace6590cb1a' }
$model = Join-Path $PSScriptRoot $name
$exe = Join-Path $PSScriptRoot 'runtime\llama-server.exe'
if (!(Test-Path -LiteralPath $exe) -or !(Test-Path -LiteralPath $model)) { throw 'Run Download-Model.ps1 first.' }
if ((Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -ne $hash) { throw 'Model checksum mismatch.' }
$portCheck = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)
try { $portCheck.Start() } catch { throw "Local port $Port is already in use." }
finally { $portCheck.Stop() }
# Keep this launcher open. Ctrl+C/failure stops its own child; no service/startup.
$arguments = @('--model', ('"' + $model + '"'), '--alias', "weasel-qwen3-$Variant",
  '--host','127.0.0.1','--port',"$Port",'--device','Vulkan0','--gpu-layers','99',
  '--ctx-size','8192','--parallel','1','--threads','6','--flash-attn','on','--jinja',
  '--reasoning','off','--reasoning-budget','0','--cache-ram','0','--no-context-shift',
  '--no-webui','--log-verbosity','1')
$readyPath = Join-Path $PSScriptRoot 'ready.json'
if (Test-Path -LiteralPath $readyPath) { Remove-Item -LiteralPath $readyPath }
$handler = [Net.Http.HttpClientHandler]::new(); $handler.UseProxy = $false
$client = [Net.Http.HttpClient]::new($handler); $client.Timeout = [TimeSpan]::FromSeconds(60)
$server = $null
try {
  $server = Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $PSScriptRoot 'runtime.stdout.log') `
    -RedirectStandardError (Join-Path $PSScriptRoot 'runtime.stderr.log')
  $clock = [Diagnostics.Stopwatch]::StartNew()
  while ($true) {
    if ($server.HasExited) { throw 'Model process exited; see runtime.stderr.log.' }
    if ($clock.Elapsed.TotalSeconds -gt 90) { throw 'Model startup timed out.' }
    try {
      $response = $client.GetAsync("http://127.0.0.1:$Port/health").GetAwaiter().GetResult()
      $healthy = $response.IsSuccessStatusCode; $response.Dispose()
      if ($healthy) { break }
    } catch {}
    Start-Sleep -Milliseconds 200
  }
  Write-Output 'Warming up with synthetic text only...'
  $body = @{model="weasel-qwen3-$Variant"; messages=@(
      @{role='system';content=('你是中文纠错检查器。仅检查错别字，保留人名代码网址，不要改写句子。' * 12)},
      @{role='user';content='只输出一个JSON对象：{"version":1,"decision":"no_change","suggestions":[]}'}
    );stream=$false;temperature=0;max_tokens=64;response_format=@{type='json_object'}} | ConvertTo-Json -Depth 6 -Compress
  $content = [Net.Http.StringContent]::new($body,[Text.Encoding]::UTF8,'application/json')
  try {
    $response = $client.PostAsync("http://127.0.0.1:$Port/v1/chat/completions",$content).GetAwaiter().GetResult()
    try { $response.EnsureSuccessStatusCode() | Out-Null; $null = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult() }
    finally { $response.Dispose() }
  } finally { $content.Dispose() }
  @{processId=$server.Id;variant=$Variant;port=$Port;startupAndWarmupMs=$clock.ElapsedMilliseconds} |
    ConvertTo-Json | Set-Content -LiteralPath $readyPath -Encoding utf8
  Write-Output "Ready: http://127.0.0.1:$Port/v1/chat/completions (weasel-qwen3-$Variant); Ctrl+C stops it."
  while (!$server.HasExited) { Start-Sleep -Milliseconds 250 }
  if ($server.ExitCode) { throw "Model exited: $($server.ExitCode)" }
} finally {
  if ($server -and !$server.HasExited) { Stop-Process -InputObject $server -Force }
  if (Test-Path -LiteralPath $readyPath) { Remove-Item -LiteralPath $readyPath }
  $client.Dispose()
}

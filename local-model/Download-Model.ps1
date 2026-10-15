param([ValidateSet('1.7b','4b')][string]$Variant = '4b')
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$files = @(
  @{ Name='llama-b11640-bin-win-vulkan-x64.zip'; Url='https://github.com/ggml-org/llama.cpp/releases/download/b11640/llama-b11640-bin-win-vulkan-x64.zip'; Hash='0a75fcbd47b0bf5b9d24246cd9c64097336ca7044a857b0785be07392b4955c8' }
)
if ($Variant -eq '4b') {
  $files += @{ Name='Qwen3-4B-Q4_K_M.gguf'; Url='https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/bc640142c66e1fdd12af0bd68f40445458f3869b/Qwen3-4B-Q4_K_M.gguf?download=true'; Hash='7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5' }
} else {
  $files += @{ Name='Qwen3-1.7B-Q8_0.gguf'; Url='https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/resolve/90862c4b9d2787eaed51d12237eafdfe7c5f6077/Qwen3-1.7B-Q8_0.gguf?download=true'; Hash='061b54daade076b5d3362dac252678d17da8c68f07560be70818cace6590cb1a' }
}
foreach ($file in $files) {
  $target = Join-Path $root $file.Name
  if (!(Test-Path -LiteralPath $target)) {
    $proxy = [System.Net.WebRequest]::GetSystemWebProxy().GetProxy([uri]$file.Url)
    $proxyArgs = @()
    if ($proxy.AbsoluteUri -ne $file.Url) { $proxyArgs = @('--proxy', $proxy.AbsoluteUri) }
    & 'C:\windows\system32\curl.exe' @proxyArgs --fail --location --retry 3 --connect-timeout 20 --speed-time 90 --speed-limit 1024 --continue-at - --output ($target + '.partial') $file.Url
    if ($LASTEXITCODE) { throw "Download failed: $($file.Name)" }
    if ((Get-FileHash -LiteralPath ($target + '.partial') -Algorithm SHA256).Hash -ne $file.Hash) { throw "Checksum mismatch: $($file.Name)" }
    Move-Item -LiteralPath ($target + '.partial') -Destination $target
  }
  if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.Hash) { throw "Checksum mismatch: $($file.Name)" }
  Write-Output "Verified $($file.Name)"
}
foreach ($file in $files | Where-Object { $_.Name.EndsWith('.zip') }) {
  if (!(Test-Path -LiteralPath (Join-Path $root 'runtime\llama-server.exe'))) {
    Expand-Archive -LiteralPath (Join-Path $root $file.Name) -DestinationPath (Join-Path $root 'runtime') -Force
  }
}
if ($Variant -eq '4b') {
  Invoke-WebRequest 'https://huggingface.co/Qwen/Qwen3-4B-GGUF/raw/bc640142c66e1fdd12af0bd68f40445458f3869b/LICENSE' -OutFile (Join-Path $root 'Qwen-4B-LICENSE.txt')
} else {
  Invoke-WebRequest 'https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/raw/90862c4b9d2787eaed51d12237eafdfe7c5f6077/LICENSE' -OutFile (Join-Path $root 'Qwen-LICENSE.txt')
}
Invoke-WebRequest 'https://raw.githubusercontent.com/ggml-org/llama.cpp/b11640/LICENSE' -OutFile (Join-Path $root 'llama-LICENSE.txt')
Write-Output 'Model and portable runtime ready.'

param(
  [ValidateSet('1.7b','4b','4b-instruct','7b')][string]$Variant = '4b',
  [string]$Destination
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$managed = ![string]::IsNullOrEmpty($Destination)
if ($managed) {
  if ($Variant -ne '4b' -or ![IO.Path]::IsPathFullyQualified($Destination)) { throw 'Expected an absolute 4B destination.' }
  $root = [IO.Path]::GetFullPath($Destination)
  if ($root -eq [IO.Path]::GetPathRoot($root)) { throw 'Use a dedicated model folder.' }
  for ($at = [IO.DirectoryInfo]::new($root); $at; $at = $at.Parent) {
    if ($at.Exists -and ($at.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'The model destination contains a directory link.' }
  }
  [void][IO.Directory]::CreateDirectory($root)
}

function Assert-PlainEntry([string]$Path) {
  $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
  if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Unexpected link: $Path" }
}

$lock = $null
if ($managed) {
  $lockPath = Join-Path $root 'download.lock'
  Assert-PlainEntry $lockPath
  $lock = [IO.File]::Open($lockPath, 'OpenOrCreate', 'Write', 'None')
}
try {
$files = @(
  @{ Name='llama-b11640-bin-win-vulkan-x64.zip'; Bytes=32159276; Url='https://github.com/ggml-org/llama.cpp/releases/download/b11640/llama-b11640-bin-win-vulkan-x64.zip'; Hash='0a75fcbd47b0bf5b9d24246cd9c64097336ca7044a857b0785be07392b4955c8' }
)
if ($Variant -eq '7b') {
  $files += @{Name='qwen2.5-7b-instruct-q4_k_m-00001-of-00002.gguf';Url='https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/bb5d59e06d9551d752d08b292a50eb208b07ab1f/qwen2.5-7b-instruct-q4_k_m-00001-of-00002.gguf?download=true';Hash='dfce12e3862a5283ccfb88221b48480e58745165de856439950d0f22590580db'}
  $files += @{Name='qwen2.5-7b-instruct-q4_k_m-00002-of-00002.gguf';Url='https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/resolve/bb5d59e06d9551d752d08b292a50eb208b07ab1f/qwen2.5-7b-instruct-q4_k_m-00002-of-00002.gguf?download=true';Hash='539cf93f78e887edea1c04e2d7d8cdaca9d01dae9c9025bcb8accbe29df3d72a'}
} elseif ($Variant -eq '4b-instruct') {
  $files += @{ Name='Qwen3-4B-Instruct-2507-Q4_K_M.gguf'; Url='https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF/resolve/a06e946bb6b655725eafa393f4a9745d460374c9/Qwen3-4B-Instruct-2507-Q4_K_M.gguf?download=true'; Hash='3605803b982cb64aead44f6c1b2ae36e3acdb41d8e46c8a94c6533bc4c67e597' }
} elseif ($Variant -eq '4b') {
  $files += @{ Name='Qwen3-4B-Q4_K_M.gguf'; Bytes=2497280256; Url='https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/bc640142c66e1fdd12af0bd68f40445458f3869b/Qwen3-4B-Q4_K_M.gguf?download=true'; Hash='7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5' }
} else {
  $files += @{ Name='Qwen3-1.7B-Q8_0.gguf'; Url='https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/resolve/90862c4b9d2787eaed51d12237eafdfe7c5f6077/Qwen3-1.7B-Q8_0.gguf?download=true'; Hash='061b54daade076b5d3362dac252678d17da8c68f07560be70818cace6590cb1a' }
}
foreach ($file in $files) {
  $target = Join-Path $root $file.Name
  $partial = $target + '.partial'
  Assert-PlainEntry $target
  Assert-PlainEntry $partial
  if ($managed -and !(Test-Path -LiteralPath $target) -and (Test-Path -LiteralPath $partial -PathType Leaf)) {
    $bytes = (Get-Item -LiteralPath $partial).Length
    if ($bytes -ge $file.Bytes) {
      if ($bytes -eq $file.Bytes -and (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -eq $file.Hash) {
        Move-Item -LiteralPath $partial -Destination $target
      } else {
        Remove-Item -LiteralPath $partial
      }
    }
  }
  if (!(Test-Path -LiteralPath $target)) {
    $proxy = [System.Net.WebRequest]::GetSystemWebProxy().GetProxy([uri]$file.Url)
    $proxyArgs = @()
    if ($proxy.AbsoluteUri -ne $file.Url) { $proxyArgs = @('--proxy', $proxy.AbsoluteUri) }
    $curl = Join-Path ([Environment]::SystemDirectory) 'curl.exe'
    if (![Environment]::Is64BitProcess -and [Environment]::Is64BitOperatingSystem) {
      $curl = Join-Path (Split-Path ([Environment]::SystemDirectory) -Parent) 'Sysnative\curl.exe'
    }
    & $curl @proxyArgs --fail --location --proto '=https' --proto-redir '=https' --retry 3 --connect-timeout 20 --speed-time 90 --speed-limit 1024 --continue-at - --output $partial $file.Url
    if ($LASTEXITCODE) { throw "Download failed: $($file.Name)" }
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $file.Hash) { throw "Checksum mismatch: $($file.Name)" }
    Move-Item -LiteralPath $partial -Destination $target
  }
  if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.Hash) { throw "Checksum mismatch: $($file.Name)" }
  Write-Output "Verified $($file.Name)"
}
foreach ($file in $files | Where-Object { $_.Name.EndsWith('.zip') }) {
  $runtime = Join-Path $root 'runtime'
  Assert-PlainEntry $runtime
  if ($managed -and !(Test-Path -LiteralPath $runtime)) {
    $staging = Join-Path $root 'runtime.partial'
    Assert-PlainEntry $staging
    if (Test-Path -LiteralPath $staging) {
      $links = Get-ChildItem -LiteralPath $staging -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }
      if ($links) { throw 'The partial runtime contains a link.' }
    }
    Expand-Archive -LiteralPath (Join-Path $root $file.Name) -DestinationPath $staging -Force
    if (!(Test-Path -LiteralPath (Join-Path $staging 'llama-server.exe') -PathType Leaf)) { throw 'Runtime executable is missing.' }
    Rename-Item -LiteralPath $staging -NewName 'runtime'
  } elseif (!$managed -and !(Test-Path -LiteralPath (Join-Path $runtime 'llama-server.exe'))) {
    Expand-Archive -LiteralPath (Join-Path $root $file.Name) -DestinationPath $runtime -Force
  }
}
if ($managed) {
  $runtimeExe = Join-Path $root 'runtime\llama-server.exe'
  Assert-PlainEntry $runtimeExe
  if (!(Test-Path -LiteralPath $runtimeExe -PathType Leaf)) { throw 'The destination contains an incomplete runtime.' }
  foreach ($name in 'Qwen-4B-LICENSE.txt','llama-LICENSE.txt','Start-LocalModel.ps1') {
    $target = Join-Path $root $name
    Assert-PlainEntry $target
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $target -Force
  }
  Write-Output 'Model and portable runtime ready.'
  return
}
if ($Variant -eq '7b') {
  Invoke-WebRequest 'https://huggingface.co/Qwen/Qwen2.5-7B-Instruct-GGUF/raw/bb5d59e06d9551d752d08b292a50eb208b07ab1f/LICENSE' -OutFile (Join-Path $root 'Qwen25-7B-LICENSE.txt')
} elseif ($Variant -eq '4b-instruct') {
  Invoke-WebRequest 'https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507/raw/4a4242ec2100137ae2dd71061babe88472e858da/LICENSE' -OutFile (Join-Path $root 'Qwen-Instruct-2507-LICENSE.txt')
} elseif ($Variant -eq '4b') {
  Invoke-WebRequest 'https://huggingface.co/Qwen/Qwen3-4B-GGUF/raw/bc640142c66e1fdd12af0bd68f40445458f3869b/LICENSE' -OutFile (Join-Path $root 'Qwen-4B-LICENSE.txt')
} else {
  Invoke-WebRequest 'https://huggingface.co/Qwen/Qwen3-1.7B-GGUF/raw/90862c4b9d2787eaed51d12237eafdfe7c5f6077/LICENSE' -OutFile (Join-Path $root 'Qwen-LICENSE.txt')
}
Invoke-WebRequest 'https://raw.githubusercontent.com/ggml-org/llama.cpp/b11640/LICENSE' -OutFile (Join-Path $root 'llama-LICENSE.txt')
Write-Output 'Model and portable runtime ready.'
} finally {
  if ($lock) { $lock.Dispose() }
}

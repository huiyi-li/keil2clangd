$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root "dist-cpp"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
g++ -std=c++17 -O2 -s -Wall -Wextra -static -static-libgcc -static-libstdc++ -o (Join-Path $outDir "Keil2JsonCpp.exe") (Join-Path $PSScriptRoot "Keil2Json.cpp")
Write-Host "Built $outDir\Keil2JsonCpp.exe"

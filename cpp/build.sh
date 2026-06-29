#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root_dir="$(cd "$script_dir/.." && pwd)"
out_dir="$root_dir/dist-cpp"
mkdir -p "$out_dir"

g++ -std=c++17 -O2 -s -Wall -Wextra \
  -o "$out_dir/Keil2JsonCpp" \
  "$script_dir/Keil2Json.cpp"

echo "Built $out_dir/Keil2JsonCpp"

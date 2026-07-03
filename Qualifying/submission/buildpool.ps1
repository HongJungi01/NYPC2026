<#
  buildpool.ps1 -- builds the strategy-variant sparring pool from variants.cpp.

  variants.cpp = frozen copy of main.cpp (as of 2026-07-03) + compile-time
  knobs. Legacy freeze (submission/POLICY.md #6): variants.cpp is no longer
  re-synced when main.cpp evolves, so the two intentionally diverge and the
  old "default preprocess == main.cpp" gate is opt-in only (-Check; useful
  solely if variants.cpp is ever re-baselined with human approval). Builds:
    var_rush.exe    (-DVARIANT_RUSH)           early-army pressure
    var_greed.exe   (-DVARIANT_GREED)          expansion / income engine
    var_turtle.exe  (-DVARIANT_TURTLE_LADDER)  defense + early ladder banking
    var_harass.exe  (-DVARIANT_HARASS)         always-on income-denial raids
  abtest.ps1 picks var_*.exe up into its opponent pool automatically.
#>
param([switch]$Check)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $root) { $root = (Resolve-Path "$PSScriptRoot\..").Path }
$sub  = Join-Path $root "submission"
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
$src     = Join-Path $sub "variants.cpp"
$mainSrc = Join-Path $sub "main.cpp"
if (-not (Test-Path $src))     { throw "missing: $src" }
if (-not (Test-Path $mainSrc)) { throw "missing: $mainSrc" }

if ($Check) {
  # only meaningful right after a (human-approved) re-baseline of variants.cpp
  $tmpV = Join-Path $env:TEMP "variants_pp.txt"
  $tmpM = Join-Path $env:TEMP "main_pp.txt"
  & g++ -E -P -std=c++17 $src     | Where-Object { $_.Trim() -ne "" } | Set-Content $tmpV -Encoding utf8
  & g++ -E -P -std=c++17 $mainSrc | Where-Object { $_.Trim() -ne "" } | Set-Content $tmpM -Encoding utf8
  $d = (Compare-Object (Get-Content $tmpV) (Get-Content $tmpM) | Measure-Object).Count
  if ($d -ne 0) { throw "variants.cpp default build differs from main.cpp ($d preprocessed lines) -- knob default broken" }
  Write-Output "[check] variants.cpp default preprocess == main.cpp  OK"
}

$variants = [ordered]@{
  rush   = "VARIANT_RUSH"
  greed  = "VARIANT_GREED"
  turtle = "VARIANT_TURTLE_LADDER"
  harass = "VARIANT_HARASS"
}
foreach ($k in $variants.Keys) {
  $exe = Join-Path $sub "var_$k.exe"
  & g++ -O2 -std=c++17 "-D$($variants[$k])" $src -o $exe
  if (-not (Test-Path $exe)) { throw "compile failed: var_$k (-D$($variants[$k]))" }
  Write-Output "built var_$k.exe  (-D$($variants[$k]))"
}
Write-Output "pool ready: $((Get-ChildItem "$sub\var_*.exe").Count) variants"

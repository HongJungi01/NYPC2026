<#
  crossplay.ps1 -- record-only cross-match harness (see submission/POLICY.md).

  Plays one snapshot ("New") against every other participant (older snapshots, plus
  any baseline cpp still present) over 10 distinct seeds, single game each (the map
  is point-symmetric, so no side-swap). Writes a summary to Logs/Test/ and raw
  referee logs to Logs/Test/raw/.

  OBSERVATION ONLY. Results are recorded, never acted on -- do NOT roll back or
  judge a change from these numbers (see POLICY.md).

  Usage:
    powershell -File submission/crossplay.ps1              # New = latest snapshot
    powershell -File submission/crossplay.ps1 -New 07011137
#>
param(
  [string]$New = "",
  [int[]]$Seeds = @(1,2,3,4,5,6,7,8,9,10)
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot   # repo Qualifying dir
if (-not $root) { $root = (Resolve-Path "$PSScriptRoot\..").Path }
$sub  = Join-Path $root "submission"
$ref  = Join-Path $root "Seongho\Game_NEXT_NATION_ver1\engine\testing-tool-practice.py"
$outDir = Join-Path $root "Logs\Test"
$rawBase = Join-Path $outDir "raw"

if (-not (Test-Path $ref)) { throw "referee not found: $ref (moved? update `$ref in crossplay.ps1)" }
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH

# Participants: 8-digit (+optional 1-letter provenance suffix r/d) snapshots + any
# baseline that still exists (hybrid retired; main/test were removed by the user --
# included only if present).
$snapshots = Get-ChildItem "$sub\*.cpp" | Where-Object { $_.BaseName -match '^\d{8}[a-z]?$' } |
             ForEach-Object { $_.BaseName } | Sort-Object
$baselines = @("main","test") | Where-Object { Test-Path (Join-Path $sub "$_.cpp") }
$participants = @($snapshots) + @($baselines)
if (-not $New) { $New = ($snapshots | Select-Object -Last 1) }
if (-not $New) { throw "no snapshot found (submission/MMDDHHmm.cpp)" }
$opponents = $participants | Where-Object { $_ -ne $New }

# Compile any missing / stale exe.
function Ensure-Exe($name) {
  $cpp = Join-Path $sub "$name.cpp"; $exe = Join-Path $sub "$name.exe"
  if (-not (Test-Path $cpp)) { throw "missing source: $cpp" }
  if ((-not (Test-Path $exe)) -or ((Get-Item $cpp).LastWriteTime -gt (Get-Item $exe).LastWriteTime)) {
    & g++ -O2 -std=c++17 $cpp -o $exe 2>$null
    if (-not $?) { throw "compile failed: $cpp" }
  }
}
Ensure-Exe $New
$opponents | ForEach-Object { Ensure-Exe $_ }

$rawDir = Join-Path $rawBase $New
New-Item -ItemType Directory -Force -Path $rawDir | Out-Null

# Play. New is LEFT (-a); LEFT_WIN=win, RIGHT_WIN=loss, else draw.
$rows = @()
foreach ($opp in $opponents) {
  $cells = @(); $w=0; $l=0; $d=0
  foreach ($s in $Seeds) {
    $log = Join-Path $rawDir "${opp}_seed$s.txt"
    python "$ref" --seed $s -l "$log" -a "$sub\$New.exe" -b "$sub\$opp.exe" | Out-Null
    $res = (Select-String -Path $log -Pattern '^RESULT ' | Select-Object -Last 1).Line
    if     ($res -match 'LEFT_WIN')  { $cells += "W"; $w++ }
    elseif ($res -match 'RIGHT_WIN') { $cells += "L"; $l++ }
    else                             { $cells += "D"; $d++ }
  }
  $rows += [pscustomobject]@{ opp=$opp; cells=$cells; tally="$w-$l-$d" }
  Write-Output "$New vs $opp : $w-$l-$d"
}

# Summary markdown.
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$hdr = "| opponent | " + (($Seeds | ForEach-Object { "s$_" }) -join " | ") + " | W-L-D |"
$sep = "|" + ("---|" * ($Seeds.Count + 2))
$lines = @(
  "# Crossplay $New (record-only)",
  "",
  "New = ``$New`` as LEFT(-a). Seeds $($Seeds -join ',') , 1 game each (map point-symmetric, no side-swap).",
  "W = $New win (LEFT_WIN) / L = loss (RIGHT_WIN) / D = draw. Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm').",
  "",
  "> OBSERVATION ONLY -- do not roll back or judge a change from these numbers (POLICY.md).",
  "",
  $hdr, $sep
)
foreach ($r in $rows) { $lines += "| ``$($r.opp)`` | " + ($r.cells -join " | ") + " | $($r.tally) |" }
$summary = Join-Path $outDir "${New}_crossplay.md"
Set-Content -Path $summary -Value $lines -Encoding utf8
Write-Output "SUMMARY: $summary"

<#
  abtest.ps1 -- paired self-play A/B for parameter/strategy tuning (see plan).

  Runs a CANDIDATE and a BASELINE bot against the SAME diverse pool (lineage
  8-digit snapshots + fixed-posture spar_* bots) over the SAME fixed seeds, so
  each (opponent, seed) game is paired. Reports W-L-D per side and the net diff.

  Methodology guard (overfitting): decision is W-L-D only, but paired seeds +
  diverse pool + seed count reduce noise. ADOPT only on a clear net gain with no
  large single-opponent regression AND a mechanistic rationale (human call).

  Usage:
    powershell -File submission/abtest.ps1 -Cand submission/cand.cpp
    powershell -File submission/abtest.ps1 -Cand submission/cand.cpp -Base submission/07011727.cpp -Seeds 20
#>
param(
  [Parameter(Mandatory=$true)][string]$Cand,
  [string]$Base = "",
  [int]$Seeds = 20,
  [string]$Out = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $root) { $root = (Resolve-Path "$PSScriptRoot\..").Path }
$sub  = Join-Path $root "submission"
$ref  = Join-Path $root "Seongho\Game_NEXT_NATION_ver1\engine\testing-tool-practice.py"
if (-not (Test-Path $ref)) { throw "referee not found: $ref" }
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH

if (-not $Base) { $Base = Join-Path $sub "07011727.cpp" }
$Cand = (Resolve-Path $Cand).Path
$Base = (Resolve-Path $Base).Path
$candName = [IO.Path]::GetFileNameWithoutExtension($Cand)
$baseName = [IO.Path]::GetFileNameWithoutExtension($Base)
$seedList = 1..$Seeds
$scratch = if ($Out) { $Out } else { Join-Path $env:TEMP "abtest_$(Get-Date -Format yyyyMMdd_HHmmss)" }
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

function Build-Exe($cpp, $exe) {
  & g++ -O2 -std=c++17 $cpp -o $exe
  if (-not (Test-Path $exe)) { throw "compile failed: $cpp" }
}

# --- accuracy gate on the candidate (SIM_TEST + OBS_TEST) ---
$gateLog = Join-Path $root "Logs\Test\raw\07011137\07010130_seed5.txt"
if (Test-Path $gateLog) {
  $simExe = Join-Path $scratch "cand_sim.exe"; $obsExe = Join-Path $scratch "cand_obs.exe"
  & g++ -O2 -std=c++17 -DSIM_TEST $Cand -o $simExe
  & g++ -O2 -std=c++17 -DOBS_TEST $Cand -o $obsExe
  $sim = & $simExe $gateLog; $obs = & $obsExe $gateLog
  if ((-not ($sim -match 'ALL OK')) -or (-not ($obs -match 'OBS OK'))) {
    Write-Output "ACCURACY GATE FAILED (candidate):"; $sim; $obs; throw "abort: candidate breaks the simulator gate"
  }
  Write-Output "[gate] candidate SIM_TEST + OBS_TEST OK"
} else {
  Write-Output "[gate] WARN: reference log missing ($gateLog) -- skipped accuracy gate"
}

# --- build contest exes for cand/base (temp names, never clobber snapshots) ---
$candExe = Join-Path $scratch "cand.exe"; $baseExe = Join-Path $scratch "base.exe"
Build-Exe $Cand $candExe
Build-Exe $Base $baseExe

# --- pool: 8-digit (+optional suffix r/d) snapshots + spar_*, excluding cand/base basenames ---
$snaps = Get-ChildItem "$sub\*.cpp" | Where-Object { $_.BaseName -match '^\d{8}[a-z]?$' } | ForEach-Object { $_.BaseName }
$spars = Get-ChildItem "$sub\spar_*.exe","$sub\var_*.exe" -ErrorAction SilentlyContinue | ForEach-Object { $_.BaseName }
$poolSnaps = $snaps | Where-Object { $_ -ne $candName -and $_ -ne $baseName }
foreach ($s in $poolSnaps) {
  $exe = Join-Path $sub "$s.exe"; $cpp = Join-Path $sub "$s.cpp"
  if ((-not (Test-Path $exe)) -or ((Get-Item $cpp).LastWriteTime -gt (Get-Item $exe).LastWriteTime)) { Build-Exe $cpp $exe }
}
$pool = @()
foreach ($s in $poolSnaps) { $pool += [pscustomobject]@{ name=$s; exe=(Join-Path $sub "$s.exe") } }
foreach ($s in $spars)     { $pool += [pscustomobject]@{ name=$s; exe=(Join-Path $sub "$s.exe") } }

# Base is also a direct opponent (head-to-head: does the change beat what it replaces?)
$h2hOpp = [pscustomobject]@{ name="$baseName(H2H)"; exe=$baseExe }

Write-Output "=== A/B  Cand=$candName  vs  Base=$baseName  |  pool=$($pool.Count) opp  |  seeds=1..$Seeds ==="

# one game: variant is -a (LEFT). returns 'W'(LEFT_WIN)/'L'(RIGHT_WIN)/'D'
function Play($varExe, $oppExe, $seed, $tag) {
  $log = Join-Path $scratch "$tag.txt"
  python "$ref" --seed $seed -l "$log" -a "$varExe" -b "$oppExe" | Out-Null
  $res = (Select-String -Path $log -Pattern '^RESULT ' | Select-Object -Last 1).Line
  if     ($res -match 'LEFT_WIN')  { return 'W' }
  elseif ($res -match 'RIGHT_WIN') { return 'L' }
  else                             { return 'D' }
}

$rows = @()
$cAgg = @{W=0;L=0;D=0}; $bAgg = @{W=0;L=0;D=0}; $flipCB=0; $flipBC=0
foreach ($opp in @($h2hOpp) + $pool) {
  $cw=0;$cl=0;$cd=0; $bw=0;$bl=0;$bd=0
  foreach ($seed in $seedList) {
    $rc = Play $candExe $opp.exe $seed "c_$($opp.name)_$seed"
    # head-to-head base plays the ACTUAL candidate as opponent; else base vs same pool opp
    if ($opp.name -like '*(H2H)*') { $rb = Play $baseExe $candExe $seed "b_$($opp.name)_$seed" }
    else                           { $rb = Play $baseExe $opp.exe  $seed "b_$($opp.name)_$seed" }
    switch ($rc) { 'W'{$cw++} 'L'{$cl++} 'D'{$cd++} }
    switch ($rb) { 'W'{$bw++} 'L'{$bl++} 'D'{$bd++} }
    if ($opp.name -notlike '*(H2H)*') {
      $cAgg[$rc]++; $bAgg[$rb]++
      if ($rc -eq 'W' -and $rb -eq 'L') { $flipCB++ }
      if ($rc -eq 'L' -and $rb -eq 'W') { $flipBC++ }
    }
  }
  $rows += [pscustomobject]@{ opp=$opp.name; cand="$cw-$cl-$cd"; base="$bw-$bl-$bd" }
}

Write-Output ""
Write-Output ("{0,-22} {1,-12} {2,-12}" -f "opponent","CAND(W-L-D)","BASE(W-L-D)")
Write-Output ("-" * 48)
foreach ($r in $rows) { Write-Output ("{0,-22} {1,-12} {2,-12}" -f $r.opp,$r.cand,$r.base) }
Write-Output ("-" * 48)
Write-Output ("POOL aggregate  CAND = {0}-{1}-{2}   BASE = {3}-{4}-{5}" -f $cAgg.W,$cAgg.L,$cAgg.D,$bAgg.W,$bAgg.L,$bAgg.D)
$netW = $cAgg.W - $bAgg.W; $netL = $cAgg.L - $bAgg.L
Write-Output ("NET (Cand-Base) : dW={0}  dL={1}   | paired flips: Cand>Base={2}  Base>Cand={3}  (net={4})" -f $netW,$netL,$flipCB,$flipBC,($flipCB-$flipBC))
Write-Output ("logs: $scratch")

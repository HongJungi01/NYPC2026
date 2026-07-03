<#
  crossplay_par.ps1 -- PARALLEL record-only cross-match harness (see POLICY.md).

  Same intent as crossplay.ps1 (New vs every other participant, N seeds each, New
  always LEFT/-a) but runs games concurrently with a throttle, since a deep-search
  bot is slow per turn and a full sequential sweep takes too long. Games are
  independent so this only changes wall-clock, not outcomes -- EXCEPT that a
  wall-clock-timed bot under CPU contention searches shallower; keep -Throttle at or
  below the physical core count so each active bot gets ~1 core.

  OBSERVATION ONLY. Results are recorded, never acted on (POLICY.md).

  Usage:
    powershell -File submission/crossplay_par.ps1                       # New=latest, all opps
    powershell -File submission/crossplay_par.ps1 -New 07030022 -Throttle 8
    powershell -File submission/crossplay_par.ps1 -Opponents 07022300d,07021544r -Seeds 1,2,3
    powershell -File submission/crossplay_par.ps1 -NewExe submission/var_nodeep.exe -NewName 07030022_nodeep -Opponents 07022300d
#>
param(
  [string]$New = "",
  [string]$NewExe = "",          # override: run this exe as New (skips compile of New)
  [string]$NewName = "",         # label for the summary when using -NewExe
  [string[]]$Opponents = @(),    # default: all participants except New
  [int[]]$Seeds = @(1,2,3,4,5,6,7,8,9,10),
  [int]$Throttle = 8
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $root) { $root = (Resolve-Path "$PSScriptRoot\..").Path }
$sub  = Join-Path $root "submission"
$ref  = Join-Path $root "Seongho\Game_NEXT_NATION_ver1\engine\testing-tool-practice.py"
$outDir = Join-Path $root "Logs\Test"
$rawBase = Join-Path $outDir "raw"
if (-not (Test-Path $ref)) { throw "referee not found: $ref" }
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH

function Ensure-Exe($name) {
  $cpp = Join-Path $sub "$name.cpp"; $exe = Join-Path $sub "$name.exe"
  if (-not (Test-Path $cpp)) { throw "missing source: $cpp" }
  if ((-not (Test-Path $exe)) -or ((Get-Item $cpp).LastWriteTime -gt (Get-Item $exe).LastWriteTime)) {
    & g++ -O2 -std=c++17 $cpp -o $exe 2>$null
    if (-not $?) { throw "compile failed: $cpp" }
  }
  return $exe
}

# Resolve participants.
$snapshots = Get-ChildItem "$sub\*.cpp" | Where-Object { $_.BaseName -match '^\d{8}[a-z]?$' } |
             ForEach-Object { $_.BaseName } | Sort-Object
$baselines = @("main","test") | Where-Object { Test-Path (Join-Path $sub "$_.cpp") }
$participants = @($snapshots) + @($baselines)

if (-not $New) { $New = ($snapshots | Select-Object -Last 1) }
if ($NewExe) {
  $newExePath = (Resolve-Path $NewExe).Path
  if (-not $NewName) { $NewName = [IO.Path]::GetFileNameWithoutExtension($newExePath) }
} else {
  $newExePath = Ensure-Exe $New
  if (-not $NewName) { $NewName = $New }
}
if (-not $Opponents -or $Opponents.Count -eq 0) {
  $Opponents = $participants | Where-Object { $_ -ne $New -and $_ -ne $NewName }
}
foreach ($o in $Opponents) { Ensure-Exe $o | Out-Null }

$rawDir = Join-Path $rawBase $NewName
New-Item -ItemType Directory -Force -Path $rawDir | Out-Null

# Build the task list.
$tasks = @()
foreach ($opp in $Opponents) {
  foreach ($s in $Seeds) {
    $log = Join-Path $rawDir "${opp}_seed$s.txt"
    $tasks += [pscustomobject]@{
      opp = $opp; seed = $s; log = $log
      oexe = (Join-Path $sub "$opp.exe")
    }
  }
}
Write-Output "New=$NewName ($newExePath)  opponents=$($Opponents.Count)  seeds=$($Seeds.Count)  tasks=$($tasks.Count)  throttle=$Throttle"

# Parallel scheduler: sliding window of Start-Process referee invocations.
$running = @()
$idx = 0
$done = 0
while ($idx -lt $tasks.Count -or $running.Count -gt 0) {
  while ($running.Count -lt $Throttle -and $idx -lt $tasks.Count) {
    $t = $tasks[$idx]; $idx++
    $argList = @("`"$ref`"","--seed",$t.seed,"-l","`"$($t.log)`"","-a","`"$newExePath`"","-b","`"$($t.oexe)`"")
    $p = Start-Process -FilePath "python" -ArgumentList $argList -NoNewWindow -PassThru `
           -WorkingDirectory $root `
           -RedirectStandardOutput "$($t.log).out" -RedirectStandardError "$($t.log).err"
    $running += [pscustomobject]@{ proc = $p; task = $t }
  }
  Start-Sleep -Milliseconds 150
  $still = @()
  foreach ($r in $running) {
    if ($r.proc.HasExited) { $done++ } else { $still += $r }
  }
  $running = $still
  Write-Progress -Activity "crossplay_par $NewName" -Status "$done / $($tasks.Count) games" `
                 -PercentComplete ([int](100 * $done / [Math]::Max(1,$tasks.Count)))
}

# Tally from the RESULT line of each log. New is LEFT: LEFT_WIN=W, RIGHT_WIN=L, else D.
$rows = @()
foreach ($opp in $Opponents) {
  $cells = @(); $w=0; $l=0; $d=0
  foreach ($s in $Seeds) {
    $log = Join-Path $rawDir "${opp}_seed$s.txt"
    $res = ""
    if (Test-Path $log) { $res = (Select-String -Path $log -Pattern '^RESULT ' | Select-Object -Last 1).Line }
    if     ($res -match 'LEFT_WIN')  { $cells += "W"; $w++ }
    elseif ($res -match 'RIGHT_WIN') { $cells += "L"; $l++ }
    else                             { $cells += "D"; $d++ }
  }
  $rows += [pscustomobject]@{ opp=$opp; cells=$cells; tally="$w-$l-$d" }
  Write-Output "$NewName vs $opp : $w-$l-$d"
}

# Clean the per-game stdout/stderr spool files.
Get-ChildItem "$rawDir\*.out","$rawDir\*.err" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

# Summary markdown.
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$tot_w = ($rows | Measure-Object -Property { [int]($_.tally -split '-')[0] } -Sum).Sum
$hdr = "| opponent | " + (($Seeds | ForEach-Object { "s$_" }) -join " | ") + " | W-L-D |"
$sep = "|" + ("---|" * ($Seeds.Count + 2))
$lines = @(
  "# Crossplay $NewName (record-only, parallel)",
  "",
  "New = ``$NewName`` as LEFT(-a). Seeds $($Seeds -join ',') , 1 game each. Throttle $Throttle.",
  "W = New win (LEFT_WIN) / L = loss (RIGHT_WIN) / D = draw. Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm').",
  "",
  "> OBSERVATION ONLY -- do not roll back or judge a change from these numbers (POLICY.md).",
  "",
  $hdr, $sep
)
foreach ($r in $rows) { $lines += "| ``$($r.opp)`` | " + ($r.cells -join " | ") + " | $($r.tally) |" }
$summary = Join-Path $outDir "${NewName}_crossplay.md"
Set-Content -Path $summary -Value $lines -Encoding utf8
Write-Output "SUMMARY: $summary"

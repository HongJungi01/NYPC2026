# run10.ps1 — redesign 기능 검증 러너 (README 개발 프로세스 정책 §2)
# 기능 exe를 ../simulator/sample-code.py(고정 상대)와 랜덤시드 N판 대전시키고
# 로그를 logs/<기능명>/ 에 남긴다. 판정은 사용자가 로그를 직접 읽고 한다.
#
# 사용:  .\run10.ps1 -Exe .\openingOptimization.exe
#        .\run10.ps1 -Exe .\foo.exe -Games 10
param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [int]$Games = 10
)

$exePath = (Resolve-Path $Exe).Path
$name = [IO.Path]::GetFileNameWithoutExtension($exePath)
$sim = (Resolve-Path (Join-Path $PSScriptRoot "..\simulator\testing-tool.py")).Path
$sample = (Resolve-Path (Join-Path $PSScriptRoot "..\simulator\sample-code.py")).Path
$outDir = Join-Path $PSScriptRoot ("logs\" + $name)
New-Item -ItemType Directory -Force $outDir | Out-Null

$summary = @()
for ($g = 1; $g -le $Games; $g++) {
  $seed = Get-Random -Minimum 1 -Maximum 1000000000
  # 양사이드 5/5: 홀수판 = 내가 LEFT, 짝수판 = 내가 RIGHT
  if ($g % 2 -eq 1) { $side = "L" } else { $side = "R" }
  $log = Join-Path $outDir ("g{0:d2}_seed{1}_{2}.txt" -f $g, $seed, $side)
  if ($side -eq "L") {
    python $sim --seed $seed -l $log -a "$exePath" -b "python $sample" | Out-Null
  } else {
    python $sim --seed $seed -l $log -a "python $sample" -b "$exePath" | Out-Null
  }
  $res = (Select-String -Path $log -Pattern '^RESULT' | Select-Object -Last 1).Line
  $line = ("g{0:d2} seed={1} me={2} : {3}" -f $g, $seed, $side, $res)
  $summary += $line
  Write-Output $line
}
$summary | Out-File (Join-Path $outDir "summary.txt") -Encoding utf8
Write-Output ("logs -> " + $outDir)

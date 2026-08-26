# A/B 벤치 CSV를 읽어 수치 게이트를 검사한다.
#
# Docs/backlog/005-ab-capture-verify.md가 정한 게이트를 그대로 구현한다.
# 게이트를 못 넘으면 임계값을 고치지 말고, 어느 항목이 얼마로 나왔는지 보고 판단할 것.
#
# 명세는 verify_bench.py를 요구했으나 이 기계에 Python이 설치돼 있지 않고
# (WindowsApps 스텁만 있어 9009로 죽는다) Scripts/ 아래가 전부 bat/ps1이라 PowerShell로 썼다.
#
# 사용법:
#   pwsh -File Scripts\verify_bench.ps1
#   pwsh -File Scripts\verify_bench.ps1 -Legacy Artifacts\bench_legacy.csv -Current Artifacts\bench_current.csv
#
# 종료 코드: 전부 통과하면 0, 하나라도 못 넘으면 1.

param(
    [string]$Legacy  = 'Docs/data/bench_legacy.csv',
    [string]$Current = 'Docs/data/bench_current.csv'
)

# 컬럼, 최소 배수, 표시 이름. legacy / current 가 이 배수 이상이어야 한다.
$gates = @(
    @{ Column = 'gpuMs';               Gate = 1.3; Label = 'GPU 프레임 시간' },
    @{ Column = 'drawCalls';           Gate = 2.0; Label = '드로우콜' },
    @{ Column = 'boneCbBytesUploaded'; Gate = 5.0; Label = '본 상수 버퍼 업로드' },
    @{ Column = 'psInvocations';       Gate = 1.5; Label = '픽셀 셰이더 호출' }
)
$minRows = 300

function Import-BenchCsv([string]$Path) {
    if (-not (Test-Path $Path)) { throw "파일이 없다: $Path" }
    $lines = Get-Content $Path
    # 첫 줄이 실행 조건을 적은 '#' 주석이면 건너뛴다.
    if ($lines.Count -gt 0 -and $lines[0].TrimStart().StartsWith('#')) {
        $lines = $lines | Select-Object -Skip 1
    }
    $rows = $lines | ConvertFrom-Csv
    if (-not $rows) { throw "데이터 행이 없다: $Path" }
    return @($rows)
}

function Get-Mean($Rows, [string]$Column) {
    if (-not $Rows[0].PSObject.Properties.Name.Contains($Column)) {
        throw "컬럼이 없다: $Column"
    }
    # 'NA'는 측정하지 않은 칸이라 평균에서 제외한다.
    $vals = foreach ($r in $Rows) {
        $v = $r.$Column
        if ($null -ne $v -and $v -ne '' -and $v -ne 'NA') { [double]$v }
    }
    if (-not $vals) { throw "컬럼에 숫자가 없다: $Column" }
    return ($vals | Measure-Object -Average).Average
}

$l = Import-BenchCsv $Legacy
$c = Import-BenchCsv $Current

Write-Output ("legacy : {0}  ({1}행)" -f $Legacy, $l.Count)
Write-Output ("current: {0}  ({1}행)" -f $Current, $c.Count)
Write-Output ''

$failures = New-Object System.Collections.Generic.List[string]

Write-Output ("{0,-22}{1,16}{2,16}{3,9}{4,8}  판정" -f '항목','legacy','current','배수','게이트')
Write-Output ('-' * 80)
foreach ($g in $gates) {
    $lv = Get-Mean $l $g.Column
    $cv = Get-Mean $c $g.Column
    $ratio = if ($cv -gt 0) { $lv / $cv } else { [double]::PositiveInfinity }
    $ok = $ratio -ge $g.Gate
    if (-not $ok) { $failures.Add(("{0}: {1:N2}배 (게이트 {2:N1}배)" -f $g.Column, $ratio, $g.Gate)) }
    Write-Output ("{0,-22}{1,16:N0}{2,16:N0}{3,9:N2}{4,8:N1}  {5}" -f `
        $g.Label, $lv, $cv, $ratio, $g.Gate, $(if ($ok) { '통과' } else { '미달' }))
}

Write-Output ''
foreach ($pair in @(@('legacy', $l), @('current', $c))) {
    $ok = $pair[1].Count -ge $minRows
    if (-not $ok) { $failures.Add(("{0} 행 수 {1} < {2}" -f $pair[0], $pair[1].Count, $minRows)) }
    Write-Output ("{0} 행 수 {1} (최소 {2}) : {3}" -f $pair[0], $pair[1].Count, $minRows, $(if ($ok) { '통과' } else { '미달' }))
}

Write-Output ''
if ($failures.Count -gt 0) {
    Write-Output '미달 항목:'
    foreach ($f in $failures) { Write-Output "  - $f" }
    Write-Output ''
    Write-Output '임계값을 고치지 말 것. 토글이 실제 코드 경로에 안 닿았거나 측정이 잘못됐을 수 있다.'
    Write-Output '두 경우 모두 아니라고 판단했다면 그 근거를 문서에 남기고 게이트를 조정할지 결정한다.'
    exit 1
}

Write-Output '모든 게이트 통과.'
exit 0

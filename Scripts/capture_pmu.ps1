param(
    [Parameter(Mandatory=$true)][string]$Backend,   # ecs|oop
    [Parameter(Mandatory=$true)][int]$N,
    [int]$Seconds = 5
)

# 저장소 루트를 스크립트 위치에서 유도한다(다른 기계에서도 그대로 돌아가도록).
$root = Split-Path -Parent $PSScriptRoot
$exe = "$root\build\bin\Release\EcsVsOopBench.exe"
$xperf = "C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe"
$tag = "$Backend`_$N"
$etl = "$root\Artifacts\pmu_$tag.etl"
$stdoutFile = "$root\Artifacts\pmu_$tag`_stdout.txt"
$dumpFile = "$root\Artifacts\pmu_$tag`_dump.txt"
$metaFile = "$root\Artifacts\pmu_$tag`_meta.txt"

if (Test-Path $etl) { Remove-Item $etl -Force }

Write-Output "=== $tag : starting xperf trace ==="
$startOut = & $xperf -on PROC_THREAD+LOADER+CSWITCH -Pmc LLCMisses,LLCReference,InstructionRetired,UnhaltedCoreCycles CSwitch strict -f $etl 2>&1
$startExit = $LASTEXITCODE
Write-Output $startOut
Write-Output "xperf -on exit code: $startExit"
if ($startExit -ne 0) {
    Write-Output "TRACE START FAILED for $tag - aborting this combo"
    exit 1
}

$p = Start-Process -FilePath $exe -ArgumentList "--isolate=$Backend","--n=$N","--op=step","--seconds=$Seconds" -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError "$stdoutFile.err"
Start-Sleep -Milliseconds 300
$tid = $null
try { $tid = (Get-Process -Id $p.Id -ErrorAction Stop).Threads[0].Id } catch { $tid = $null }
Write-Output "PID=$($p.Id) TID=$tid"
$p.WaitForExit()

$stopOut = & $xperf -stop 2>&1
Write-Output $stopOut

"PID=$($p.Id)`nTID=$tid`nBackend=$Backend`nN=$N`nSeconds=$Seconds" | Out-File -Encoding ascii $metaFile

Write-Output "=== $tag : dumping trace to text ==="
& $xperf -i $etl -tle -tti -o $dumpFile -a dumper 2>&1 | Select-Object -Last 5

Write-Output "=== $tag : done. dump=$dumpFile meta=$metaFile stdout=$stdoutFile tid=$tid ==="

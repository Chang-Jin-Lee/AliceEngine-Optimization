# 측정 데이터

[ECS_VS_OOP.md](../ECS_VS_OOP.md)와 [ECS_VS_OOP_LOWLEVEL.md](../ECS_VS_OOP_LOWLEVEL.md)가 근거로 삼은 실행본입니다.
문서의 모든 수치는 이 파일들에서 나왔습니다.

| 파일 | 내용 |
|---|---|
| `ecs_vs_oop.csv` | N 스윕 96행. 시간·할당·메모리 |
| `ecs_vs_oop_env.txt` | 그 실행의 하드웨어·빌드 구성·반복 정책 |
| `pmu_summary.csv` | 하드웨어 캐시 카운터 정규화 결과 |
| `pmu/*_stdout.txt` | xperf 원본 출력 |
| `pmu/*_meta.txt` | 각 트레이스의 수집 조건 |
| `bench_legacy.csv` | 렌더링 최적화 **이전** 경로. 598프레임 |
| `bench_current.csv` | 렌더링 최적화 **이후** 경로. 598프레임 |

뒤의 두 파일은 최상위 [README](../../README.md)의 결과 표가 나온 실행이다.
`Scripts/run_bench.bat`이 같은 카메라 테이크를 두 경로로 재생해 뽑는다. 검산해 보려면 이렇게 하면 된다.

```powershell
$d = Get-Content Docs\data\bench_current.csv | Select-Object -Skip 1 | ConvertFrom-Csv
($d | ForEach-Object { [double]$_.gpuMs } | Measure-Object -Average).Average   # 5.88
```

`bench_legacy.csv`의 같은 계산이 51.21이다. 첫 줄은 실행 조건을 적은 주석이라 건너뛰어야 한다.

`Artifacts/`는 벤치를 다시 돌리면 생성되는 작업 디렉터리입니다. gitignore되어 있습니다.
여기 있는 파일은 문서가 인용한 그 실행의 사본이라 덮어쓰이지 않습니다.
직접 돌려서 나온 결과를 여기 있는 파일과 비교해 보시면 됩니다.

재실행 방법은 [ECS_VS_OOP.md](../ECS_VS_OOP.md)의 재현 절에 있습니다.
같은 수치가 나오지는 않습니다. 코드를 건드리지 않은 상태에서도 실행 간 중앙값이 최대 20%까지 움직입니다.
문서가 배수를 소수점 한 자리까지만 쓰는 이유입니다.

## `ecs_vs_oop_env.txt` 8행에 대하여

그 줄은 이렇게 적혀 있습니다.

```
참고: N=50000에서도 ECS 약 4.6MB, OOP 약 16MB로 둘 다 L3(36MB) 안에 들어간다.
```

**두 숫자는 틀렸습니다.** 메모리 측정 방식을 고치기 전에 손으로 계산한 추정치가 문자열로 박혀 있었고
그 뒤 `_msize` 기반 실측으로 바뀌었는데 이 줄만 따라가지 못했습니다.
실측값은 ECS 6,472,830 B(6.17 MiB), OOP 10,340,262 B(9.86 MiB)입니다.
`ecs_vs_oop.csv`의 `peakLiveBytes` 컬럼에서 확인하실 수 있습니다.
OOP 쪽 추정이 특히 커서, 옛 값은 실제보다 62% 높게 잡혀 있었습니다.

문자열은 소스에서 고쳤습니다. 여기 있는 파일은 고치지 않았습니다.
이건 실제로 돌아간 실행의 출력 그대로여야 하고 나중에 손댄 파일은 근거가 되지 못하기 때문입니다.
결론(둘 다 L3 안에 들어가므로 이 벤치는 DRAM이 아니라 캐시 계층을 잰다)은 실측값으로도 그대로입니다.

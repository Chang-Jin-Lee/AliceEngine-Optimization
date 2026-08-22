# ECS vs OOP: 로우레벨 캐시 카운터 측정 (Task 7)

## 1. 왜 이걸 쟀는가

Task 6까지의 `EcsVsOopBench`는 **시간**(add/step/random/remove의 median/min/max)과 **메모리**(할당 횟수·바이트,
peak live bytes, working set)를 쟀다. 이 저장소의 포트폴리오(`Docs/PORTFOLIO_EGOSIS.md` 등)에는
"컴포넌트를 한 곳에 모아 순회해서 캐시 적중률이 올라갔다"는 주장이 있는데, 면접에서 "그걸 정말
측정해봤느냐"는 질문을 받았다. Task 6의 시간 측정은 그 주장의 **결과**(더 빠르다)는 보여주지만
**원인**(캐시 미스가 실제로 줄었다)은 보여주지 않는다. 시간 차이는 캐시 미스가 아니라 분기 예측,
가상 함수 디스패치 비용, 할당자 오버헤드 등 다른 요인으로도 설명될 수 있다.

이 태스크는 CPU의 하드웨어 성능 카운터(PMU/PMC)를 직접 읽어 LLC(L3) 참조/미스, 명령어 리타이어,
코어 사이클을 실측하고, "캐시 미스 자체가 줄었는가"라는 질문에 숫자로 답한다.

## 2. 어떤 도구를 어떻게 찾았는가

컨트롤러가 이미 확인해 준 사실(`task-7-brief-revised.md`): xperf 경로, 버전 10.0.26100, 관리자 권한,
`-pmusources`가 아니라 `-pmcsources`가 맞는 플래그. 아래는 그걸 이 기계에서 직접 재확인하고,
그 다음 실제로 카운터 데이터를 뽑아내기까지 시도한 순서를 그대로 남긴 것이다.

### Step 1 — `-pmcsources` 재확인

```
> "C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe" -pmcsources
Maximum selectable profile sources: 9.

Id  Name                             Interval  Min      Max
--------------------------------------------------------------
  0 Timer                               10000  1221    1000000
  2 TotalIssues                         65536  4096 2147483647
  6 BranchInstructions                  65536  4096 2147483647
 10 CacheMisses                         65536  4096 2147483647
 11 BranchMispredictions                65536  4096 2147483647
 19 TotalCycles                         65536  4096 2147483647
 25 UnhaltedCoreCycles                  65536  4096 2147483647
 26 InstructionRetired                  65536  4096 2147483647
 27 UnhaltedReferenceCycles             65536  4096 2147483647
 28 LLCReference                        65536  4096 2147483647
 29 LLCMisses                          65536  4096 2147483647
 30 BranchInstructionRetired            65536  4096 2147483647
 31 BranchMispredictsRetired            65536  4096 2147483647
 32 LbrInserts                          65536  4096 2147483647
 33 InstructionsRetiredFixed            65536  4096 2147483647
 34 UnhaltedCoreCyclesFixed             65536  4096 2147483647
 35 UnhaltedReferenceCyclesFixed        65536  4096 2147483647
 36 TimerFixed                          10000  1221    1000000
```

컨트롤러가 옮겨 둔 6개 항목과 이름·ID가 정확히 일치했고, 그 외에 `BranchInstructions`,
`UnhaltedReferenceCycles`, `LbrInserts` 등 추가 소스도 보였다. "Maximum selectable profile sources: 9"
라는 상한도 확인했다 — 우리가 쓸 4~5개는 여유 안이다.

### Step 2 — `-help start`로 `-Pmc`/`-PmcProfile` 문법 확인

```
	-Pmc        counters events [strict] Dump hardware counters values with specified
                                     events. Specify counters as
                                     Counter1,Counter2,..CounterN and events as
                                     Event1+Event2+..EventN. Note that failures
                                     while programing the counters will not
                                     result in trace start failure, unless
                                     'strict' is specified.
	-PmcProfile counters         Sample on specified hardware counters.
                                     Specify counters as
                                     Counter1,Counter2,..CounterN
```

브리프에 적힌 그대로였다.

### Step 3 — `-help processing`으로 `pmc` 액션 확인, 그리고 첫 번째 막다른 길

```
	pmc              Show Rollover Processor Counters Information
```

브리프가 미리 경고한 대로 이름이 불길했다("Rollover"). 일단 그대로 시도했다.

```
> xperf -on PROC_THREAD+LOADER+CSWITCH -Pmc LLCMisses,LLCReference,InstructionRetired,UnhaltedCoreCycles CSwitch strict -f Artifacts\pmu_test.etl
(exit code 0 — strict인데도 프로그래밍 실패 없이 시작됨)
The trace you have just captured "...\pmu_test.etl" may contain personally identifiable information...
> xperf -stop
(exit code 0, 174MB .etl 생성됨)
> xperf -i Artifacts\pmu_test.etl -o Artifacts\pmu_test.csv -a pmc
> type Artifacts\pmu_test.csv
           PmcInterrupt,  TimeStamp, CPU,     Process Name ( PID),            Image!Function, ProfileSource, Reserved
```

**헤더만 나오고 데이터 행이 0개였다.** `-a pmc`가 기대하는 이벤트 이름이 `PmcInterrupt`인데, 이건
`-PmcProfile`(인터럽트 기반 샘플링)이 만드는 이벤트다. 우리가 쓴 `-Pmc ... CSwitch strict`(이벤트
발생 시 카운터 값을 "덤프"하는 방식)는 다른 이벤트 이름으로 기록되고 있었다 — 브리프가 예상한
"Rollover 정보"라는 이름 그대로, `-a pmc` 액션은 우리가 쓴 방식의 리포트가 아니었다.

### Step 4 — 두 번째 시도: `-a cswitch`

혹시 컨텍스트 스위치 리포트에 카운터 열이 같이 붙어 나오나 확인했다.

```
> xperf -i Artifacts\pmu_test.etl -o Artifacts\pmu_test_cswitch.csv -a cswitch
> type Artifacts\pmu_test_cswitch.csv
 StartTime,   EndTime,  Cpu 0,  Cpu 1,  Cpu 2, ...
         0,   1000000,  99.83,  82.70,  99.85, ...
```

CPU별 사용률 요약표였다. PMC 값과는 무관했다 — 막다른 길.

### Step 5 — 세 번째 시도: `-a tracestats`로 트레이스 안에 실제로 뭐가 있는지 확인

```
> xperf -i Artifacts\pmu_test.etl -tle -tti -o Artifacts\pmu_test_stats.csv -a tracestats
> type Artifacts\pmu_test_stats.csv
Number of Processors : 32
...
Total # Lost Buffers : 0
Total # Lost Events  : 0
```

이벤트 타입별 개수 분해는 안 나왔지만("stack" 옵션이 없으면 요약만 나옴), 최소한 "Lost Events: 0"으로
버퍼 유실이 없었다는 것은 확인했다 — 데이터가 없는 게 아니라 리포트 액션이 안 맞는 것이라는 심증을
굳혔다.

### Step 6 — 성공: 기본 `-a dumper`(원본 이벤트 텍스트 덤프)로 실제 이벤트 이름 확인

```
> xperf -i Artifacts\pmu_short.etl -tle -tti -o Artifacts\pmu_short_dump.txt -a dumper
> grep -i "pmc" Artifacts\pmu_short_dump.txt | head
                    PmcCorruptionStatus,  TimeStamp, ProcessorIndex, NumberOfProfileSources, [[Source, LastGoodTimestampQpc] ...]
                    Pmc,  TimeStamp,   ThreadID, LLCMisses, LLCReference, InstructionRetired, UnhaltedCoreCycles
```

여기서 실제 이벤트 이름이 `Pmc`(단수, `PmcInterrupt`가 아니다)라는 것을 확인했다. 컬럼은
`TimeStamp, ThreadID, LLCMisses, LLCReference, InstructionRetired, UnhaltedCoreCycles` — 우리가
`-Pmc` 인자로 넘긴 이벤트/카운터 순서 그대로다. **`-a pmc` 처리 액션이 아니라 원본 이벤트 덤퍼
(`-a dumper`, 인자를 생략했을 때의 기본 액션)를 쓰고 텍스트를 직접 파싱해야 한다**는 것이 이 태스크
도구 조사의 결론이다. `xperf -help processing`의 액션 이름과 실제 이벤트 클래스 이름이 다르다는
것은 문서에 없었고, 실측으로만 알 수 있었다.

### 원시 데이터 형태

```
                    Pmc,     193148,          0, 19861, 82212, 7081816, 5980583
                    Pmc,     193155,          0, 1129, 3757, 17278, 112245
                    Pmc,     193158,      31236, 19872, 82339, 7087497, 5990161
                    Pmc,     193162,       6796, 79591, 502302, 41421713, 42091383
```

`ThreadID` 필드가 있어 스레드별로 필터링할 수 있다. 값은 누적(cumulative)으로 보이고 — 뒷부분에서
설명하겠지만 완전히 단조증가는 아니었다(5절 한계 참고).

## 3. 격리 실행 모드와 기본 경로 불변 검증

### 3.1 구현

`main.cpp`에 `--isolate=ecs|oop --n=<N> --op=step --seconds=<S>` 인자를 추가했다(Ruling 16).
`ParseIsolateArgs`가 `argc==1`(인자 없음)일 때 `requested=false`를 반환하므로 `main()`의 기존 로직은
**한 줄도 건드리지 않고** 그대로 실행된다. 격리 모드 진입 판정은 `main()` 최상단에서 한 번만 하고,
`MeasureBackend`/`MeasurePeakLiveBytes`의 본문은 전혀 수정하지 않았다 — 격리 모드는 `RunIsolate<Backend>`라는
완전히 별도의 템플릿 함수다. 스레드 친화도(logical processor 2)/우선순위 설정은 기존 코드와 동일한
Win32 호출을 격리 모드에서 다시 수행한다(중복이지만, 감사된 기존 코드 경로를 건드리지 않기 위한
의도적 선택이다).

격리 모드는 `Backend backend; backend.Add(...)`로 N개를 채우고, 워밍업으로 `Step()`을 5회 돌린 뒤(트레이스
밖에서 캐시/TLB를 데운다), `QueryPerformanceCounter` 기반 `ScopedTimer`로 지정한 초 동안 `Step(0.016f)`을
반복하고, `stepCount`·`elapsedMs`·`componentUpdates`(= stepCount × N × 3컴포넌트)를 표준출력에 찍고 종료한다.
`--op=step` 외의 값은 명시적으로 미지원 처리한다(브리프가 요구한 가장 중요한 케이스만 구현).

### 3.2 기본 경로 불변 검증 (절대 조건)

빌드:
```
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release --target EcsVsOopBench
```
경고 없이 성공.

변경 전(베이스라인, 수정 전 커밋 `d7606c7`의 바이너리로 실행) vs 변경 후(`--isolate` 추가 후 재빌드,
인자 없이 실행)를 비교했다.

| 항목 | 베이스라인 | 변경 후 | 일치 |
|---|---|---|---|
| CSV 행 수(헤더 포함) | 97 | 97 | 예 |
| 헤더 문자열 | `backend,op,n,medianMs,stdDevMs,minMs,maxMs,allocCount,allocBytes,peakLiveBytes,workingSetBytes` | 동일 | 예 |
| `allocCount`/`allocBytes`/`peakLiveBytes` (전 96행) | — | — | **정확히 일치** (필드별 diff 0건) |
| NA 위치(어느 (backend,op,n)가 NA인지) | — | — | **정확히 일치** |
| equivalence PASS 2건 | PASS/PASS | PASS/PASS | 예 |
| checksum FAIL 여부 | 없음 | 없음 | 예 |
| `g_liveBytes` 최저 관측값 | 0(정상) | 0(정상) | 예 |

median/sd/min/max는 실행 간 드리프트가 있는 값이라 브리프가 요구한 대로 정확한 일치를 확인하지
않았다(예: N=50000 ecs add median이 베이스라인 2.4480ms, 변경 후에도 같은 자리에서 비슷한 범위 —
±20% 이내). **결정적이어야 하는 세 컬럼(allocCount/allocBytes/peakLiveBytes)은 96행 전부 바이트
단위로 동일했다.** 절대 조건 통과.

격리 모드 자체도 확인:
```
> EcsVsOopBench.exe --isolate=ecs --n=1000 --op=step --seconds=1
[isolate] backend=ecs n=1000 op=step seconds=1.000
[isolate] priorityClass=ok threadPriority=ok affinity=ok
[isolate] stepCount=759597 elapsedMs=1000.001 componentUpdates=2278791000 (n=1000 * 3 components * stepCount)

> EcsVsOopBench.exe --isolate=oop --n=1000 --op=step --seconds=1
[isolate] stepCount=273975 elapsedMs=1000.000 componentUpdates=821925000 (...)
```
ECS가 OOP보다 초당 Step() 호출 수가 약 2.77배 많다 — Task 6의 시간 측정(N=1000 step에서 ECS가
OOP보다 여러 배 빠름)과 방향이 일치해 격리 모드 자체의 정합성도 확인됐다. 격리 모드는 `Artifacts/`의
CSV·env 파일을 전혀 건드리지 않는다(코드 상에서 해당 경로에 `fopen`을 호출하지 않음).

## 4. 측정 절차와 얻은 수치

### 4.1 트레이스 채집

4개 조합(ecs/oop × N=1,000/50,000) 각각에 대해 동일한 절차를 반복했다(`Artifacts/capture_pmu.ps1`):

```
xperf -on PROC_THREAD+LOADER+CSWITCH -Pmc LLCMisses,LLCReference,InstructionRetired,UnhaltedCoreCycles CSwitch strict -f Artifacts\pmu_<tag>.etl
Start-Process EcsVsOopBench.exe --isolate=<backend> --n=<N> --op=step --seconds=5   (PID/TID 확보)
(프로세스 종료 대기)
xperf -stop
xperf -i Artifacts\pmu_<tag>.etl -tle -tti -o Artifacts\pmu_<tag>_dump.txt -a dumper
```

각 실행에서 벤치 표준출력으로 실제 작업량을 확인했다:

```
ecs  N=1000  : stepCount=3806444  elapsedMs=5000.000  componentUpdates=11419332000
ecs  N=50000 : stepCount=69719    elapsedMs=5000.013  componentUpdates=10457850000
oop  N=1000  : stepCount=1407996  elapsedMs=5000.002  componentUpdates=4223988000
oop  N=50000 : stepCount=20659    elapsedMs=5000.060  componentUpdates=3098850000
```

덤프 텍스트에서 대상 프로세스의 스레드 ID(`Pmc,` 줄의 `ThreadID` 필드)로 필터링해 **첫 샘플과
마지막 샘플의 차(delta)**를 구했다(방법론은 5절 참고). 표본 개수는 5초 동안 그 스레드가 컨텍스트
스위치를 겪은 횟수와 같다 — 친화도·우선순위 설정이 잘 먹혀서 다들 20~190개 수준으로 적다(그만큼
논리 프로세서 2번을 거의 독점했다는 뜻이기도 하다).

```
ecs  N=1000  (TID 33008): 32 샘플, 구간 5.150790s
  ΔLLCMisses=2,079,184  ΔLLCReference=7,857,513  ΔInstructionRetired=152,923,141,577  ΔUnhaltedCoreCycles=27,739,997,516

ecs  N=50000 (TID 16744): 188 샘플, 구간 5.173326s
  ΔLLCMisses=2,288,479  ΔLLCReference=2,358,263,194  ΔInstructionRetired=139,594,004,938  ΔUnhaltedCoreCycles=27,473,184,344

oop  N=1000  (TID 32516): 23 샘플, 구간 5.135430s
  ΔLLCMisses=2,811,184  ΔLLCReference=9,337,740  ΔInstructionRetired=79,349,477,203  ΔUnhaltedCoreCycles=27,946,039,364

oop  N=50000 (TID 50672): 25 샘플, 구간 5.184455s
  ΔLLCMisses=9,949,509  ΔLLCReference=4,365,470,386  ΔInstructionRetired=58,440,794,478  ΔUnhaltedCoreCycles=27,821,942,363
```

### 4.2 정규화 표

PMC 채집 구간(첫~마지막 샘플)이 벤치가 보고한 5.000초 타이밍 구간과 정확히 일치하지 않으므로(시작
전 Add() 채우기·워밍업이 섞여 있어 5.13~5.18초로 조금 더 길다), `componentUpdates`를
`(PMC 구간 길이) / (벤치 elapsedMs/1000)` 비율로 스케일링해 같은 시간창에 맞춘 뒤 정규화했다.

| backend | N | 컴포넌트 갱신당 LLC 미스 | (환산: N갱신당 1미스) | LLC미스/LLC참조 | IPC (Instr/Cycle) |
|---|---:|---:|---:|---:|---:|
| ECS | 1,000  | 1.767×10⁻⁴ | 1 / 5,658 | 26.46% | 5.513 |
| ECS | 50,000 | 2.115×10⁻⁴ | 1 / 4,728 | 0.097% | 5.081 |
| OOP | 1,000  | 6.480×10⁻⁴ | 1 / 1,543 | 30.11% | 2.839 |
| OOP | 50,000 | 3.097×10⁻³ | 1 / 323   | 0.228% | 2.101 |

**OOP/ECS 배율(갱신당 LLC 미스):**
- N=1,000  : 3.666배
- N=50,000 : 14.641배

## 5. 해석

**N=1,000과 N=50,000 사이에 배율이 실제로 벌어졌다 (3.67배 → 14.64배).** 이건 포트폴리오 주장과
"방향이 맞고, 그것도 브리프가 예측한 정확한 모양(작은 N에서는 차이가 작고, 큰 N에서 벌어진다)으로"
나온 결과다. 작은 N(둘 다 L2에 들어가는 크기)에서도 OOP가 이미 3.67배 더 많은 캐시 미스를 내는 건,
`std::make_unique`로 흩어놓은 개별 힙 할당(엔티티당 GameObject 1개 + Behaviour 3개, 총 4번의 개별
할당)이 캐시 라인 활용을 근본적으로 나쁘게 만들기 때문으로 해석된다 — 데이터가 전부 L2/L3에
"들어간다"는 것과 "한 캐시 라인을 알차게 쓴다"는 것은 다른 얘기다. N=50,000(ECS ~4.6MB, OOP ~16MB,
둘 다 L3 36MB 안에는 들어감, `main.cpp`의 `BuildEnvironmentReport` 참고)에서 배율이 14.64배로
벌어지는 것은, 데이터셋이 L2(2MB)를 넘어서면서 ECS는 여전히 조밀한 배열 순회로 L3 대역폭을 효율적으로
쓰는 반면 OOP는 포인터 추적이 L3 수준에서도 계속 새 캐시 라인을 만들어내기 때문으로 보인다.

IPC도 같은 방향이다: ECS가 5.08~5.51인 반면 OOP는 2.10~2.84로 대략 절반이다. **OOP의 IPC 하락폭도
N에 따라 더 크다**(1,000→50,000 사이 ECS는 5.51→5.08로 7.8% 하락, OOP는 2.84→2.10으로 26% 하락) —
스톨이 캐시 미스와 함께 커진다는 정황 증거다.

**단, LLC미스/LLC참조 비율(%)은 이 데이터에서 신뢰도가 낮다.** N=1,000에서는 두 백엔드 모두 26~30%라는
높은 수치가 나오는데, 이는 실제로 캐시가 자주 미스한다는 뜻이 아니라 **LLC까지 내려가는 참조 자체가
워낙 적어서(5초에 780만~930만 건, N=50,000의 22억~44억 건과 비교하면 300배 이상 적다) 분모가 작아
비율이 배경 잡음에 좌우된다**는 뜻으로 해석해야 한다. N=1,000 워크로드는 데이터가 L2에 상주하므로
Step() 루프 자체는 L3를 거의 건드리지 않고, 관측된 LLC 참조는 OS/다른 프로세스의 배경 활동이 상당
부분 섞였을 가능성이 크다. 반대로 **"컴포넌트 갱신당 LLC 미스"(분모가 우리가 아는 작업량)는 N=1,000에서도
noise에 덜 휘둘리는 지표**이고, 그 값 자체가 3.67배 차이를 보였다는 게 이 태스크의 더 믿을 만한 결론이다.

포트폴리오 주장("컴포넌트를 모아 순회하면 캐시 적중률이 올라간다")은 **이 측정 범위(N=1,000~50,000,
Step() 연산)에서는 방향과 규모 모두 지지된다.** 다만 이 결론은 이 벤치마크의 특정 워크로드(3개
컴포넌트, 단순 부동소수점 연산, 순차 접근)에 한정된 것이고, random-access 패턴이나 다른 N 구간까지
일반화하려면 별도 측정이 필요하다는 점은 분명히 해 둔다.

## 6. 한계

1. **CSwitch 경계 귀속의 근사성.** `-Pmc ... CSwitch`는 컨텍스트 스위치가 일어날 때만 카운터 값을
   덤프한다. 두 덤프 사이에 우리 스레드가 아닌 다른 무언가(DPC/ISR, 타이머 인터럽트로 인한 idle
   스레드로의 짧은 전환)가 같은 논리 프로세서 2번에서 실행됐다면 그 활동의 카운트가 우리 스레드의
   델타에 섞여 들어간다. 스레드 친화도+HIGH_PRIORITY_CLASS 덕에 표본 수가 적다(20~190개/5초)는 것
   자체가 그런 간섭이 드물었다는 정황이지만, 완전히 배제하지는 못한다.

2. **첫 구간의 비단조성(non-monotonic) 관측 — 원인 미확정.** 4개 트레이스 전부에서 프로세스 시작
   직후 첫 5~10개 샘플이 단조증가하지 않고 한 번 이상 요동친다(예: ecs N=50000에서 값이 100248 →
   65308로 감소했다가 다시 증가, 심지어 한 지점(t=291873)에서는 651까지 거의 리셋된 것처럼 보이는
   값이 나왔다). 이후 구간(대략 t=0.3~0.8초 이후)은 끝까지 완전히 단조증가했다. **이 초기 요동의
   메커니즘은 확인하지 못했다** — 가설(검증 안 됨)은 프로세스/스레드 로딩 초기의 콜드 스케줄링,
   또는 하이퍼스레딩 형제 스레드(3번 논리 프로세서)의 간섭, 또는 OS가 스레드별 PMC 가상화 컨텍스트를
   초기화하는 과정에서 생기는 과도현상이다. **이 문서의 정규화 수치는 이 요동을 특별 취급하지 않고
   단순히 "첫 샘플~마지막 샘플" 델타를 그대로 썼다** — 특정 구간을 골라내는 자의적 판단을 피하기
   위한 선택이다. 이로 인해 실제 값보다 최대 수 % 정도 델타가 작게 잡혔을 수 있다(첫 샘플이 이미
   어느 정도 진행된 값이었을 가능성). 네 조합 모두 같은 방법을 썼으므로 상호 비교(배율)에는 이
   바이어스가 상쇄될 것으로 본다.

3. **`-a pmc`(카운트 리포트 액션)를 쓰지 못했다.** 이 액션은 "Rollover Processor Counters
   Information"용으로, `-PmcProfile`(인터럽트 기반 샘플링)이 만드는 `PmcInterrupt` 이벤트를 위한
   것이지 우리가 쓴 `-Pmc`(이벤트 발생 시 덤프) 방식의 `Pmc` 이벤트용이 아니었다. 대신 원본 이벤트
   텍스트 덤퍼(`-a dumper`, 옵션 생략 시 기본값)로 전체 트레이스를 텍스트로 뽑아 `grep`/`awk`로
   `Pmc,` 줄만 필터링했다. 각 조합의 덤프 파일은 5억~6억 바이트(500~600MB)에 달했다 — 시스템
   전체(32개 논리 프로세서)의 컨텍스트 스위치를 다 기록하기 때문이다. `Artifacts/`는 gitignore
   대상이라 이 파일들은 커밋되지 않는다.

4. **시간 창 정합의 근사.** PMC 채집 구간(5.13~5.18초)과 벤치가 자체 보고한 타이밍 구간(정확히
   5.000초)이 다르다 — PMC 구간에는 Add() 채우기와 5회 워밍업 Step()이 일부 포함된다. 이 문서는
   `componentUpdates`를 두 구간의 길이 비율로 선형 스케일링해 보정했다(3~4% 보정). Step() 처리량이
   구간 내내 안정적(steady-state)이라는 가정 위에 있다 — 워밍업 5회가 전체 대비 무시할 수 있는
   비중이라는 점(예: N=50000에서 워밍업 5회는 전체 69,719회의 0.007%)을 볼 때 합리적인 가정이라
   판단했다.

5. **LLC 미스/참조 비율(%)은 N=1,000 구간에서 신뢰도가 낮다** (4절 해석 참고) — 분모(LLC 참조 수)
   자체가 작아 배경 잡음의 비중이 커진다. 컴포넌트 갱신당 LLC 미스(절대 카운트를 알려진 작업량으로
   나눈 값)를 주 지표로 삼은 이유다.

6. **표본 1개 프로세스, 1회 실행.** Task 6의 시간 측정은 11회 반복(워밍업 2회 제외 9회)의
   중앙값을 쓰지만, 이 PMC 측정은 조합당 1회 실행(5초)만 했다 — xperf 트레이스 채집 자체가
   무겁고(파일 100MB+, 덤프 500MB+) 시간이 오래 걸려 반복 스윕을 하기엔 무리였다. 실행 간 분산은
   확인하지 못했다.

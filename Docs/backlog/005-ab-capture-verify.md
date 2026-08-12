# 005 — A/B 캡처와 검증

004까지 끝난 상태에서 두 실행을 돌려 수치와 영상을 만듭니다.
사람 눈이 필요한 판단을 없애는 것이 이 단계의 설계 목표입니다. CSV로 숫자를 검증하고 PNG를 직접 열어 화면을 검증합니다.

## 선행 조건

- 001 ~ 004의 완료 조건을 모두 통과
- `Bench/take01.json`이 존재. 없으면 004의 요청 문구를 그대로 띄우고 멈춤
- Release 빌드
- ffmpeg이 PATH에 있음. 없으면 설치를 요청하고 멈춤

## 실행 순서

`--frames` 없이 계측용으로 두 번, `--frames`를 붙여 영상용으로 두 번 돌립니다.
004에 적은 대로 PNG 덤프가 프레임 시간을 왜곡하므로 두 목적을 섞지 않습니다.

```bat
set EXE=build\bin\EGOSIS.exe
set TAKE=Bench\take01.json
set COMMON=--camera-replay=%TAKE% --vsync=off --width=1920 --height=1080 --warmup=5 --duration=15

REM 1. 계측 — legacy
%EXE% %COMMON% --legacy --csv=Artifacts\bench_legacy.csv

REM 2. 계측 — current
%EXE% %COMMON%          --csv=Artifacts\bench_current.csv

REM 3. 영상 소재 — legacy
%EXE% %COMMON% --legacy --frames=Artifacts\legacy\%%06d.png  --frame-stride=1

REM 4. 영상 소재 — current
%EXE% %COMMON%          --frames=Artifacts\current\%%06d.png --frame-stride=1
```

`Scripts/run_bench.bat`으로 저장해 두십시오. 다음에 다시 돌릴 때 조건이 흔들리지 않습니다.

## 수치 게이트

두 CSV를 읽어 워밍업 이후 구간의 평균을 계산하고 아래를 확인합니다.
계산 스크립트는 `Scripts/verify_bench.py`로 남기십시오.

| 항목 | 게이트 |
|---|---|
| `gpuMs` 평균 | legacy ≥ current × 1.3 |
| `drawCalls` 평균 | legacy ≥ current × 2.0 |
| `boneCbBytesUploaded` 평균 | legacy ≥ current × 5.0 |
| `psInvocations` 평균 | legacy ≥ current × 1.5 |
| CSV 줄 수 | 양쪽 모두 300줄 이상 |
| 줄 수 차이 | 두 CSV의 줄 수 차이가 큰 것은 정상. legacy가 느려서 프레임이 적음 |
| 폐기 프레임 | 전체의 5% 이하. 넘으면 GPU 클럭이 불안정한 것이므로 재실행 |

**게이트를 못 넘으면 임계값을 고치지 마십시오.** 토글이 실제 코드 경로에 안 닿았거나 측정이 잘못된 것입니다.
어느 항목이 얼마로 나왔는지 수치와 함께 보고하고 멈추십시오.

## 화면 검증

각 PNG 디렉터리에서 균등 간격으로 5장을 골라 **직접 열어서** 확인합니다.

1. 게임 화면이 렌더되어 있는가. 검은 화면이나 클리어 색만 있으면 실패
2. 타일이 화면에 다수 보이는가. 테이크가 타일 밀집 구간을 지났는지 확인
3. 오버레이 숫자가 읽히는가. `frame`, `CPU`, `GPU`, `drawCalls`가 판독 가능해야 함
4. legacy 쪽 상태 배지에 legacy가 켜져 있다고 표시되는가
5. 프레임타임 그래프가 그려져 있는가

한 항목이라도 실패하면 인코딩하지 말고 무엇이 안 보이는지 보고하십시오.

## 인코딩

```bat
ffmpeg -y -framerate 60 -i Artifacts\legacy\%%06d.png ^
  -c:v libx264 -crf 18 -preset slow -pix_fmt yuv420p -movflags +faststart Artifacts\legacy.mp4

ffmpeg -y -framerate 60 -i Artifacts\current\%%06d.png ^
  -c:v libx264 -crf 18 -preset slow -pix_fmt yuv420p -movflags +faststart Artifacts\current.mp4
```

`-framerate`는 테이크의 `fixedDeltaSeconds`에서 나온 값을 쓰십시오. 0.0166667이면 60입니다.
실제 실행 프레임레이트가 아닙니다. PNG는 프레임 인덱스 순서라 테이크 기준으로 재생해야 두 영상의 재생 시간이 같습니다.

## 결과 문서

`Docs/OPTIMIZATION_AB_RESULT.md`를 만들어 다음을 적습니다. **수치는 CSV에서 계산한 값만 쓰고 손으로 쓰지 마십시오.**

- 실행 조건 표 — GPU, 드라이버, 해상도, vsync, 빌드 구성, 씬, 테이크 파일, 측정 구간
- 비교 표 — 항목별 legacy / current / 배수. 평균과 함께 min, max, 1% low
- 켠 legacy 항목 목록과 각 항목의 `OPTIMIZATION_REPORT` ID
- 패스별 GPU ms 비교 표. **이 표가 "드로우콜이 세 겹이었다"는 문장보다 강합니다**
- 영상 두 개의 경로
- 한계 — PNG 덤프 실행의 프레임 시간은 계측에 쓰지 않았다는 점, 녹화 오버헤드, 단일 머신 측정

## 완료 조건

1. `Artifacts/bench_legacy.csv`, `Artifacts/bench_current.csv`가 존재하고 양쪽 300줄 이상
2. 수치 게이트 전부 통과
3. PNG 검증 5항목 전부 통과
4. `Artifacts/legacy.mp4`, `Artifacts/current.mp4`가 존재하고 각 10초 이상. `ffprobe`로 길이를 확인
5. `Docs/OPTIMIZATION_AB_RESULT.md`가 CSV 계산값으로 채워짐
6. `Scripts/run_bench.bat`, `Scripts/verify_bench.py`가 커밋되어 재실행 가능
7. `.gitignore`에 `Artifacts/`가 있음. 영상과 CSV는 저장소에 넣지 않음

## 사람이 이어서 할 일

에이전트는 완료 보고에 이 안내를 함께 남기십시오.

- 영상 두 개를 유튜브에 올리고 링크를 노션 포트폴리오의 EGOSIS 항목에 넣기
- 자막에 실행 조건을 넣기. 오버레이 상태 배지에 이미 남아 있으므로 중복이면 생략 가능
- 나란히 놓은 비교 영상을 만들 경우, 두 mp4의 재생 시간이 같으므로 영상 편집기에서 좌우로 붙이면 프레임이 맞음
- 외부 도구를 겹쳐 한 번 더 찍어 두면 자체 계측 수치의 교차 검증이 됩니다. PresentMon 2.x 오버레이가 API 무관하게 FPS, 프레임타임, CPU Busy, GPU Busy를 띄웁니다

## 한 테이크 전환 영상 (선택)

003에서 `F10` 전체 토글을 한 프레임 안에 반영하도록 만들었으므로,
한 번의 실행에서 키를 눌러 전환하는 영상도 찍을 수 있습니다.
같은 씬, 같은 카메라, 같은 프레임이라는 것이 영상 안에서 증명되므로 두 영상을 나란히 놓는 것보다 설득력이 큽니다.
이 촬영은 사람이 직접 하는 편이 낫습니다. 전환 타이밍을 잡아야 하기 때문입니다.

# 004 — 벤치 모드와 카메라 테이크

같은 씬, 같은 카메라 경로에서 두 번 실행할 수 있게 만듭니다.
**카메라 경로는 사람이 직접 한 번 날아서 녹화합니다.** 에이전트가 카메라 경로를 만들지 마십시오.

## 왜 재생인가

손으로 두 번 날면 두 구간이 달라집니다. 그러면 "조건이 다른데 비교가 되냐"는 질문을 막을 수 없습니다.
한 번 녹화해서 두 실행이 같은 파일을 재생하면 프레임 단위로 같은 장면이 보장되고,
사람 손은 테이크를 찍는 한 번만 들어갑니다.

## 커맨드라인 인자

`Samples/Sandbox/Main.cpp`의 `wWinMain`은 지금 `PWSTR` 인자를 무시합니다(23행).
`GetCommandLineW()` + `CommandLineToArgvW`로 파싱하는 경로를 추가하십시오.

```
EngineSource/Engine/src/Runtime/Engine/CommandLineOptions.h
EngineSource/Engine/src/Runtime/Engine/CommandLineOptions.cpp
```

| 인자 | 기본값 | 의미 |
|---|---|---|
| `--scene=<path>` | 없음 | 시작 시 로드할 씬. `Assets/Scenes/` 기준 상대경로 |
| `--camera-record=<path>` | 없음 | 카메라 테이크 녹화. 이 인자가 있으면 사람이 조작하는 모드 |
| `--camera-replay=<path>` | 없음 | 카메라 테이크 재생. 입력을 무시하고 파일이 카메라를 구동 |
| `--legacy` | off | 003의 플래그 전부 켜기 |
| `--vsync=on\|off` | on | `--vsync=off`면 `Present(0, 0)` |
| `--duration=<sec>` | 0 | 지정 시간이 지나면 자동 종료. 0이면 수동 종료 |
| `--warmup=<sec>` | 5 | 이 시간 동안은 CSV에 기록하지 않음 |
| `--csv=<path>` | 없음 | 프레임별 계측 CSV 출력 |
| `--frames=<pattern>` | 없음 | 프레임 PNG 덤프. 예 `Artifacts/legacy/%06d.png` |
| `--frame-stride=<n>` | 1 | n프레임마다 한 장 덤프 |
| `--width`, `--height` | 1920, 1080 | 백버퍼 크기 고정 |

`--camera-record`와 `--camera-replay`를 함께 주면 오류로 종료합니다.

## vsync

`Runtime/Rendering/D3D11/D3D11RenderDevice.cpp` 199행이 `m_swapChain->Present(1, 0)`로 고정돼 있습니다.
`SyncInterval`을 런타임 값으로 바꾸십시오. **vsync가 켜져 있으면 프레임 시간이 16.67ms에 붙어서 개선이 안 보입니다.**
벤치 실행에서는 반드시 끕니다.

## 카메라 테이크 포맷

`Bench/take01.json`. 사람이 만든 입력 자산이므로 커밋합니다.

```json
{
  "version": 1,
  "scene": "BossRoom338.scene",
  "fixedDeltaSeconds": 0.0166667,
  "frameCount": 900,
  "frames": [
    { "p": [12.0, 3.5, -40.0], "q": [0.0, 0.0, 0.0, 1.0], "fovY": 0.785398 }
  ]
}
```

`Runtime/Rendering/Camera.h`의 `SetPosition`, `SetRotation`(쿼터니언 `XMFLOAT4`), `SetPerspective`를 그대로 씁니다.

**녹화** — 사람이 에디터 카메라로 날아다니는 동안 프레임마다 `GetPosition`, `GetRotationQuat`, `GetFovYRadians`를 기록합니다.
프레임 시간이 흔들려도 되지만 기록은 `fixedDeltaSeconds` 슬롯에 맞춰 리샘플링해 저장하십시오.
그러지 않으면 재생 쪽 프레임레이트가 달라질 때 경로가 어긋납니다.

**재생** — 프레임 인덱스로 테이크를 읽어 카메라에 직접 넣습니다. 재생 중에는 카메라 입력을 무시합니다.
프레임레이트와 무관하게 **프레임 인덱스 기준**으로 진행해야 합니다. 시간 기준으로 보간하면
legacy 실행이 느려서 같은 시간에 더 짧은 경로를 지나게 되고, 두 영상의 장면이 어긋납니다.
`frameCount`를 넘으면 종료합니다.

게임플레이 상태(애니메이션, 파티클, 물리)도 재생 구간에서 결정적이어야 합니다.
`--camera-replay`가 켜지면 델타 타임을 `fixedDeltaSeconds`로 고정하십시오. 실제 프레임 시간을 쓰면 두 실행의 씬 상태가 갈라집니다.

## CSV 포맷

헤더 한 줄, 프레임마다 한 줄입니다. `Disjoint`로 폐기된 프레임은 쓰지 않습니다.

```
frame,presentMs,cpuMs,gpuMs,gpu_MainPass,gpu_CameraPreview,gpu_ComputeEffects,gpu_ParticleOverlay,gpu_DebugOverlay,gpu_ToneMapAndUI,gpu_OverlayEffects,drawCalls,instancedDrawCalls,iaPrimitives,vsInvocations,psInvocations,cPrimitives,boneCbMapCount,boneCbBytesUploaded,vramUsedMB,workingSetMB
```

첫 줄 앞에 `#` 주석으로 실행 조건을 남기십시오. 나중에 이 CSV만 보고도 조건을 알 수 있어야 합니다.

```
# legacy=on vsync=off 1920x1080 scene=BossRoom338.scene take=Bench/take01.json build=Release gpu=<어댑터 이름> driver=<버전>
```

## PNG 덤프

백버퍼를 스테이징 텍스처로 복사해 저장합니다. 오버레이가 그려진 뒤, Present 직전에 캡처해야 HUD가 함께 담깁니다.
`--frame-stride`로 장수를 줄일 수 있게 하십시오. 900프레임 전체를 PNG로 뽑으면 디스크가 부담입니다.

**덤프가 프레임 시간을 왜곡합니다.** 그러므로 다음 규칙을 지키십시오.

- 계측 CSV를 만드는 실행과 PNG를 덤프하는 실행을 **분리**합니다. CSV용 실행은 `--frames` 없이 돌립니다.
- PNG 덤프 실행은 영상 소재용이며, 그 실행의 CSV는 쓰지 않습니다.
- 이 분리를 CSV 주석과 완료 보고에 명시하십시오.

## 사람이 해야 하는 단계

이 지점에서 에이전트는 멈추고 요청합니다.

```
Bench/take01.json 이 없습니다. 카메라 테이크를 한 번 녹화해 주세요.

  build\bin\EGOSIS.exe --scene=<타일이 밀집한 씬> --camera-record=Bench/take01.json --vsync=off

  - 타일이 많이 보이는 구간을 15초 정도 날아 주세요
  - 시작과 끝을 급하게 움직이지 마세요. 앞 5초는 워밍업으로 버립니다
  - 끝나면 창을 닫으면 파일이 저장됩니다

저장되면 알려 주세요. 이어서 005를 진행합니다.
```

파일이 없는 상태로 임의 경로를 만들어 진행하지 마십시오.

씬 후보는 `Assets/Scenes/`에서 배경 타일이 많은 것을 고릅니다. 렌더독 기록의 문제 장면이 보스 방이었으므로
`tmpBossScene.scene`, `tmpBossScene2.scene`, `PrototypeMap_Art.scene`, `#01PrototypeMap.scene` 순으로 열어 보고
타일 수가 가장 많은 것을 제안하십시오. 최종 선택은 사람에게 맡깁니다.
씬을 열어 타일 개수를 세는 것이 어렵다면 `.scene` JSON에서 타일 프리팹 참조 수를 세도 됩니다.

## 완료 조건

1. `Build.bat`이 성공합니다.
2. `--vsync=off`로 실행하면 오버레이의 프레임 시간이 16.67ms에 붙지 않습니다.
3. `--camera-record`로 5초 날면 `frames` 배열이 채워진 JSON이 저장되고, `frameCount`가 배열 길이와 같습니다.
4. 같은 테이크를 `--camera-replay`로 두 번 재생했을 때, 마지막 프레임의 카메라 위치가 두 실행에서 소수점 셋째 자리까지 같습니다. **결정적 재생 확인입니다.**
5. `--duration=10 --csv=...`로 실행하면 CSV가 생기고, 줄 수가 대략 `10초 × 실제 프레임레이트`이며, `--warmup` 구간이 빠져 있습니다.
6. `--frames`로 실행하면 PNG가 생기고, 그중 한 장을 **직접 열어** 게임 화면과 오버레이가 모두 담겼는지 확인합니다.
7. 인자 없이 그냥 실행했을 때 기존 동작이 그대로입니다. vsync는 켜진 상태, 오버레이는 뜨고, 씬은 원래 시작 씬입니다.

## 실패 시

4번을 못 넘기면 재생이 결정적이지 않습니다. 델타 타임 고정과 프레임 인덱스 기준 진행을 다시 확인하십시오.
이 조건을 못 맞추면 005의 A/B 비교가 성립하지 않으므로 넘어가지 마십시오.

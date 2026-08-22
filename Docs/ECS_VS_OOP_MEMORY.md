# 5,000 엔티티 씬의 워킹셋 40GB 조사

시연 씬([EcsStress5000](../Assets/Scenes/Bench/EcsStress5000.scene))에서 엔진 HUD의 working set이
약 40GB로 찍혔다. 대조군(42 엔티티)은 589MB였다. 그 차이가 무엇인지 조사한 기록이다.

결론부터: **누수가 아니다.** 씬 로드 중에 끝나는 유한한 비용이고, 엔티티당 8.0MiB가 CPU 측에서 든다.
정확한 할당 지점은 특정하지 못했다. 어디까지 좁혔는지와 무엇을 배제했는지를 남긴다.

## 측정 조건

i9-13900KF, 128GB DDR5-4800, RTX 4060 Ti, Windows 11 Pro 10.0.26200, Release 빌드.
프로세스 메모리는 HUD를 믿지 않고 `System.Diagnostics.Process`의 `WorkingSet64`로 바깥에서 따로 쟀다.
씬 변형은 `EcsStress5000.scene`을 원본으로 삼아 필요한 필드만 바꿔 생성했다.

## 누수가 아니다

가장 먼저 확인한 것이 이것이다. 짧은 창으로 보면 메모리가 250ms마다 약 107MiB씩 선형으로 늘어
누수처럼 보인다. 150초를 관찰하니 다른 그림이 나왔다.

| t(초) | 워킹셋 |
|---:|---:|
| 0.5 | 66 MiB |
| 25 | 6,983 MiB |
| 50 | 17,214 MiB |
| 75 | 27,240 MiB |
| 100 | 37,543 MiB |
| 110 | 40,446 MiB |
| 115 이후 | 40,335 MiB (변화 없음) |

110초에 걸쳐 늘다가 **정확히 평평해진다.** 마지막 30초 기울기는 0.0 MiB/s다.
짧은 창에서 보이던 "선형 증가"는 로딩 구간이었다. 프레임 루프의 누수가 아니다.

같은 씬을 `--csv`로 19,480프레임 돌린 결과도 이를 뒷받침한다. 워킹셋이 2,740MiB에서 2,541MiB로
오히려 줄었다. 즉 렌더 루프는 메모리를 늘리지 않는다.

## 엔티티당 8.0MiB, CPU 측

엔티티 수를 바꿔가며 첫 프레임 시점의 값을 쟀다.

| 엔티티 | 워킹셋 | VRAM |
|---:|---:|---:|
| 42 | 648 MiB | 1,870 MiB |
| 102 | 1,130 MiB | 1,742 MiB |
| 302 | 2,731 MiB | 1,845 MiB |

워킹셋 증가분은 (2,731−648)÷(302−42) = **엔티티당 8.01MiB**로 정확히 선형이다.
반면 **VRAM은 엔티티 수와 무관하게 일정하다.** GPU 자원은 정상적으로 공유되고 있다는 뜻이고,
문제는 CPU 측 할당이다.

값이 첫 CSV 프레임에서 이미 최종치라는 점도 중요하다. 할당은 첫 렌더 프레임 이전, 씬 로드 중에 끝난다.

## 배제한 것들

가설을 세우고 하나씩 실험으로 떨어뜨렸다.

**메시 애셋에 달렸다 — 맞다.** 같은 5,002 엔티티 씬에서 메시만 `tile0205`로 바꾸니 총 99MiB에
증가율 −11 MiB/s로 아무 일도 일어나지 않는다. `Alice_Swimsuit_white`일 때만 발생한다.

**인스턴싱 경로 때문이다 — 아니다.** `boneCount`를 0(인스턴싱)과 1(비인스턴싱)로 바꿔 재보니
404.0 대 403.6 MiB/s로 동일했다. 나중에 알았지만 씬 로더가 `SceneFile.cpp:1168`에서 `boneCount = 1`을
강제하므로 이 실험은 애초에 두 조건을 만들지도 못했다.

**Material 컴포넌트 때문이다 — 아니다.** 302 엔티티에서 Material을 전부 제거해도 2,648 → 2,669MiB로
변화가 없다. 로그에 엔티티 수만큼(5,000회) 찍히는 `[MaterialFile] Load:`는 눈에 띄지만 메모리의
원인은 아니다. 다만 엔티티마다 같은 `.mat`을 다시 읽는 것은 그 자체로 낭비다.

**FBX 임포트가 엔티티마다 일어난다 — 아니다.** 302 엔티티 씬의 로그에서 `Import start`,
`Registered mesh key`, `Import done`이 각각 **2회**뿐이다. 메시 등록은 공유되고 있다.

**PhysX가 엔티티마다 충돌 메시를 굽는다 — 아니다.** 이 씬에는 `Phy_Settings` 컴포넌트가 없어
`RefreshPhysicsForCurrentWorld`가 `EnginePhysics.cpp:24`에서 곧바로 반환한다.

**에디터 피킹이 CPU 정점 사본을 캐시한다 — 아니다.** `ViewportPicker`에 정점 캐시가 없다.

**SkinnedMesh 컴포넌트가 트리거인 것은 맞다.** 302 엔티티에서 `SkinnedMesh`를 떼면 2,731MiB가
**213MiB**로 떨어진다. 그런데 컴포넌트 자체는 문자열 두 개와 포인터, `uint32` 하나가 전부다
(`SkinnedMeshComponent.h:13-19`). 즉 컴포넌트를 소비하는 쪽에서 무언가가 만들어진다.

## 8.0MiB가 무엇인지에 대한 단서

임포트 로그가 이 메시의 형상을 알려준다.

```
Registered mesh key="Alice_Swimsuit_white" stride=108 indexCount=115896 subsets=13 mats=13
```

8.0MiB(8,388,608 B)를 stride 108로 나누면 정점 77,672개다. 이 메시의 정점 수로 그럴듯한 값이다.
즉 **정점 배열 한 벌이 엔티티마다 CPU 측에 잡히는 것으로 보인다.** 다만 이걸 만드는 코드를 찾지 못했다.

`SkinnedMeshRegistry::Find`는 `shared_ptr<SkinnedMeshGPU>`를 돌려주므로 복사가 아니고,
`sourceModel`도 `shared_ptr`로 공유된다. 씬 로더의 `SkinnedMesh` 블록은 `AddComponent` 후
필드 세 개를 대입할 뿐이다.

## 힙 트레이스

Release에 PDB를 켜고(`/Zi` + `/DEBUG` + `/OPT:REF` + `/OPT:ICF`) 다시 떴다.
`/Zi`는 디버그 정보만 별도 파일로 낼 뿐 코드 생성을 바꾸지 않는다. 다만 `/DEBUG`가 기본적으로
`/OPT:REF`와 `/OPT:ICF`를 끄므로 둘을 명시해 되돌려야 Release와 같은 바이너리가 나온다.

xperf로 힙을 잡을 때 두 번 막혔고 둘 다 기록해 둔다.

- `-heap`만 켜면 `-a heap`이 프로세스를 인식하지 못하고 `Alloc#`이 전부 0으로 나온다.
  모듈 로드 정보가 없어서다. 커널 세션(`-on PROC_THREAD+LOADER`)을 따로 켜고 `xperf -merge`로
  합쳐야 주소가 모듈로 풀린다.
- `-symbols`는 액션 플래그가 아니라 전역 플래그다. `-a dumper -symbols`가 아니라
  `-symbols -a dumper` 순서여야 한다.

32 엔티티 씬으로 25만 건의 `HeapAlloc`을 잡아 스택의 첫 호출자별로 묶은 결과다.
할당자 래퍼(`operator new`, `malloc` 등)는 건너뛰고 실제 호출 지점으로 집계했다.

| 할당량 | 건수 | 평균 | 호출 지점 |
|---:|---:|---:|---|
| 2,211 MiB | 70,666 | 32 KB | `Alice::InputSystem::Update` |
| 1,428 MiB | 28 | 53 MB | `std::vector<unsigned char>::_Buy_raw` |
| 1,265 MiB | 24 | 55 MB | `Alice::ResourceManager::LoadBinary` |
| 109 MiB | 3,538 | 32 KB | `FbxModel::Load` |
| 107 MiB | 500 | 224 KB | `CreateTextureFromWIC` |
| 77 MiB | 107,469 | 752 B | `DeferredRenderSystem::CreateShaders` |

여기서 두 가지가 나왔다. 둘 다 엔티티당 8MiB와는 별개이고, 그 자체로 고칠 값어치가 있다.

### 1. `LoadBinaryAuto`의 복사 — 고쳤지만 측정 효과는 없었다

`ResourceManager.cpp:377-387`에 이런 코드가 있다.

```cpp
bool ResourceManager::LoadBinaryAuto(const std::filesystem::path& logicalPath,
                                     std::vector<std::uint8_t>& outData) const
{
    outData.clear();
    if (auto sp = LoadSharedBinaryAuto(logicalPath))
    {
        outData = *sp; // 호환 API: 복사
        return true;
    }
    return false;
}
```

`LoadSharedBinaryAuto`는 해시 기반 블롭 캐시에서 `shared_ptr`를 돌려주는데, 호환 API인
`LoadBinaryAuto`가 그걸 받아 전체를 복사한다. 읽기만 하는 호출자 네 곳
(`DeferredRenderSystem.cpp`, `ForwardRenderSystem.cpp`의 텍스처 로더, `FbxMaterial.cpp`,
`UIRenderer.cpp`)을 `LoadSharedBinaryAuto`로 바꿔 복사를 없앴다.
소유권이 필요한 `SoundManager.cpp:475`는 그대로 뒀다. 버퍼를 맵에 보관하고 FMOD에 포인터를 넘기며,
`FMOD_OPENMEMORY_POINT`로 전환할 계획이 주석에 남아 있기 때문이다.

**그런데 같은 조건으로 다시 트레이스를 떠 보니 수치가 바뀌지 않았다.**
`_Buy_raw` 1,427.7MiB/28건, `LoadBinary` 1,264.6MiB/24건, `CreateSRVFromMemoryWithType` 7.4MiB/19건이
수정 전과 바이트 단위로 동일했다.

처음에 이 1.26GiB를 "불필요한 복사"로 읽은 것은 **오독이었다.** 그 프레임은
`ResourceManager::LoadBinary`, 즉 파일을 캐시로 읽어들이는 함수다. 파일당 한 번씩 일어나는 정상적인
로딩 비용이고 내 수정과 무관하다. 실제로 이 씬에서 큰 텍스처를 읽는 경로는 내가 바꾼 네 곳이 아니라
`ResourceLoader<SRV>::Load` 쪽(`CreateTextureFromWIC` 500건)이었다.

수정 자체는 유지한다. 불필요한 복사를 없앤 것은 그 자체로 옳고, 렌더링 무결성도 확인했다
(프로토타입맵 드로우콜 6,700, 스트레스 씬 505로 수정 전과 동일). 다만 **이 수정으로 메모리나
로딩 시간이 개선됐다고 말할 근거는 없다.**

### 2. `InputSystem::Update`가 초당 7,000회 할당한다

10초 실행에서 70,666회, 합계 2.2GiB다. **엔티티 수와 무관하다** — 32 엔티티 씬에서 나온 수치다.
프레임마다 해제되므로 상주 메모리로는 안 잡히지만, 입력 처리가 프레임당 수백 회 할당할 이유는 없다.

## 아직 못 찾은 것

**엔티티당 8.0MiB가 어느 줄에서 나오는지는 여전히 특정하지 못했다.**
힙 트레이스는 할당 *총량*을 보여주지 실제로 붙들고 있는 양(할당 − 해제)을 보여주지 않는다.
위 표의 큰 항목들은 전부 에셋 로드(약 26회)이거나 프레임당 churn이라 엔티티 수에 비례하지 않는다.

남은 후보는 `CreateTextureFromWIC`이다. 32 엔티티에서 500회면 엔티티당 약 15회이고,
이 메시의 머티리얼 수가 13개라는 점과 맞아떨어진다. 다만 VRAM이 엔티티 수에 비례하지 않았으므로
GPU 텍스처가 엔티티마다 생기는 것은 아니고, CPU 측 디코드 버퍼가 남는 형태여야 설명이 된다.
확인하려면 할당과 해제를 짝지어 상주량을 계산하는 분석이 필요하다.

## 곁가지로 발견한 것

애니메이션 시스템 세 곳이 컴포넌트 맵을 **매 프레임 통째로 복사**한다.

- `AdvancedAnimSystem.h:44`
- `AnimBlueprintSystem.h:40`
- `SkinnedAnimationSystem.h:33`

셋 다 `auto skinnedMap = world.GetComponents<SkinnedMeshComponent>();`인데 `const auto&`여야 한다.
`Inspector_Physics.cpp:496`도 같다. 이번 메모리 문제와는 별개지만 실재하는 낭비다.

## 시연 씬을 쓸 때 알아둘 것

이 씬은 ECS와 GameObject-Component 비교의 시각적 보조 자료다.
로드에 약 110초가 걸리고 약 40GB를 쓴다. 128GB 미만인 기계에서는 스와핑이 일어날 수 있다.
엔티티 수를 줄이면 비례해서 줄어든다(엔티티당 8MiB). 300개면 약 2.7GB다.

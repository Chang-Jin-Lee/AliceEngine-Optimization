# Alice Engine (EGOSIS) 엔진 평가 보고서

> 평가일: 2026-06-30
> 평가 대상: EGOSIS_Refactoring / Alice Engine (자체 제작 C++20 · Direct3D 11 엔진)
> 평가 관점: 상용화 가능성 · 사용 편의성 · 안전성/보안 · 빌드·배포 가능성
> 비교군: 해외·국내 자체엔진 12종+

---

## 총점: **64 / 100**

> **"3주·8인 팀이 만든 자체엔진으로서는 상위 5~10%에 드는 완성도. 다만 '상용 엔진 제품'의 절대 기준에서는 니치 인디 게임을 *제 손으로 출시*하는 도구 수준이며 *남에게 파는/널리 배포되는* 엔진까지는 거리가 있다."**

채점은 **"실제 상용 엔진 제품"이라는 절대 기준**으로 매겼다. 같은 엔진을 *"학생/포트폴리오 자체엔진"* 기준으로 보면 **약 90점**이다 — 둘은 다른 자(尺)이며 아래에서 분리한다.

---

## 1. 항목별 점수

| 평가 항목 | 점수 | 근거 (코드 감사 기반) |
|---|---:|---|
| **빌드 & 배포 가능성** | **76** | 에디터(`Launch`)와 **독립 실행 게임(`AlicePlayer`) 타깃 분리**. 에디터 내 `BuildGameWindow`가 *원클릭 패키징* 수행 → CMake 릴리스 빌드 + 스크립트 DLL 빌드 + **에셋 쿠킹(.alice, 256KB 청크 XOR 암호화)** + 설정 export → `Export/Bin/`에 exe·dll·Cooked 산출. **비개발자가 더블클릭 실행 가능.** 감점: Windows 전용 산출물, 인스톨러 없음, vcpkg 잠금파일 없음. |
| **렌더링 · 기술 성숙도** | **78** | Forward+Deferred 이중 경로, PBR/**Toon PBR**, Shadow Map(PCF), Bloom, **Post-Process Volume**(언리얼식), Decal(D-Buffer), 트레일·파티클·**Compute VFX**. `IRenderDevice` 추상화가 깨끗해 백엔드 교체 여지 있음. 감점: D3D11 전용, LOD·스트리밍 없음(대규모 월드 불가). |
| **사용 편의성** | **58** | ImGui 에디터 6패널(Hierarchy/Inspector/Project/Viewport/Camera/Lighting), **Undo/Redo, ImGuizmo, 프리팹 D&D, RTTR 자동 인스펙터, AnimBlueprint 노드 에디터, 머티리얼 에디터**. Unity식 API, 문서 양호. 감점: **C++/CMake/vcpkg/다중 솔루션 장벽**, Windows 전용, 핫리로드 별도 DLL 빌드, 씬 JSON 수기 편집, 한글 주석, 신규원 적응 2~3주. 노코드 아님. |
| **안전성 · 견고함** | **56** | 경로 traversal 방어 강력(화이트리스트+`lexically_normal`), 스마트포인터 우세(255건 vs raw new/delete 41건), assert/체크 691건, 스레드 안전 파일 로깅. 감점: **크래시 핸들러·미니덤프 없음**(공개 배포 치명적), CFG/ASLR 등 런타임 하드닝 미설정, 대규모 미검증. |
| **보안** | **60** | **네트워크 코드 0줄(오프라인 → 공격면 최소)**, DLL 로드 경로 제한+export 검증, 리테일 정적 DLL 바인딩, RTTR JSON 타입 안전. 감점: **XOR은 암호가 아닌 난독화**, DLL 서명·에셋 무결성 검증 없음, exe 코드 서명 없음. |
| **상용화 준비도(엔진 제품으로서)** | **48** | 자기 게임(단일 보스전) 출시엔 충분·검증됨. 그러나 *범용 엔진 제품*으로는 생태계·서드파티 문서·지원·크로스플랫폼·검증 타이틀 수 모두 부족. |

---

## 2. "상용화"의 두 가지 뜻 — 분리해서 봐야 함

| 해석 | 평가 | 점수 |
|---|---|---:|
| **(a) 이 엔진으로 상용 게임을 출시할 수 있나?** | **가능.** Steam Early Access·itch.io 등 큐레이션 채널로 *소규모 인디 타이틀* 출시는 현실적. 실제 게임으로 검증됨. | ~72 |
| **(b) 이 엔진 자체를 제품으로 팔/배포할 수 있나?** | ️ **아직 아님.** 단일 검증 타이틀, Windows 전용, 서드파티 온보딩·지원·생태계 부재. | ~45 |

→ **"게임을 낸다"는 충분히 되고 "엔진을 판다"는 멀었다.**

---

## 3. 자체엔진 12종+ 비교

### 해외 AAA 자체엔진 (S티어 — 도달 목표)
| 엔진 | 회사 | 특징 |
|---|---|---|
| RE Engine | Capcom | 멀티플랫폼, 고효율 |
| Decima | Guerrilla | 대규모 지형 스트리밍·식생 |
| Frostbite | EA/DICE | 범용 멀티장르 |
| REDengine | CD Projekt | 오픈월드·서사 |
| id Tech | id Software | 극한 최적화 |
| Northlight | Remedy | 레이트레이싱(Alan Wake 2) |

### 국내 자체엔진 (희소 — 대부분 UE/Unity로 전환)
| 엔진 | 회사 | 상태 |
|---|---|---|
| **BlackSpace Engine** | **Pearl Abyss** | 국내 유일 최상위. Black Desert(2015) 엔진을 차세대로 재설계, **풀 패스트레이싱**, Crimson Desert·DokeV·Plan8 구동. GDC 2025 발표. |
| MapleStory 초기 엔진 | Nexon | 2001년 DX8/VC++6로 *직접* 제작(당시 범용엔진 부재) — 역사적 자체엔진 |
| Epic Seven 인하우스 | Smilegate | 구형 기기 지원·로딩 최적화용 모바일 자체엔진 |
| (Lineage 계열) | NCsoft | 과거 자체기술 → 현재 Throne and Liberty 등 **UE로 전환**(자체엔진 포기 사례) |

> **시사점:** 국내는 자체엔진을 끝까지 끌고 간 곳이 Pearl Abyss 정도. 큰 회사도 대부분 UE/Unity로 갔다는 건, 자체엔진 상용화가 그만큼 어렵다는 방증이다.

### 오픈소스·인디 자체엔진 (A/B티어 — EGOSIS의 실제 이웃)
| 엔진 | 성격 | EGOSIS 대비 |
|---|---|---|
| **Flax Engine** | C#/C++ 핫리로드, 에디터 완비 | 목표상 형. EGOSIS가 도달하려는 방향 |
| **Stride** | 오픈소스 C# 엔진(구 Silicon Studio) | 〃 |
| **O3DE** | Amazon/Linux Foundation, Vulkan/DX12 | 규모·이식성 압도 |
| bgfx / The Forge / Diligent | 렌더링 프레임워크(엔진 아님) | EGOSIS는 그 위 "엔진" 계층까지 보유 → 더 완결적 |
| **Falling Everything**(Noita) | C++ 자체엔진, **상용 출시 성공** | EGOSIS와 같은 "인디 자체엔진 상용화" 증명 사례 |
| The Witness 엔진(Thekla) | C++ 자체엔진, 상용 출시 | 〃 |

---

## 4. EGOSIS의 좌표

```
AAA 인하우스 (BlackSpace, RE Engine)  ─ 규모·하드닝·이식성·생태계 압도
        ▲   ← 큰 격차 (인년 단위)
오픈소스 productized (Flax/Stride/O3DE) ─ EGOSIS의 "도달 목표"
        ▲   ← 생태계·크로스플랫폼·서드파티 지원 격차
EGOSIS (Alice Engine) ───────────────  "인디 게임을 출시할 수 있는 완결형 자체엔진"
        ▼   ← EGOSIS가 앞서는 지점: 에디터·쿠킹·ECS·문서까지 갖춤
전형적 학생/취미 엔진 (렌더러+씬 정도, 배포 파이프라인 없음)
```

EGOSIS는 **"Noita/The Witness처럼 인디 타이틀을 실제로 출시 가능한 자체엔진" 등급**이다. 전형적 학생 엔진보다 확연히 위, Flax/Stride 같은 *제품화된* 오픈소스 엔진보다는 아래(생태계·이식성·하드닝 차이).

---

## 5. 점수를 끌어올리려면 (우선순위)

1. **크래시 핸들러·미니덤프 추가**(Crashpad/Breakpad) — 공개 배포 최소 요건. 안전성 56→70.
2. **XOR → 실제 무결성**(HMAC 서명) + **exe 코드 서명** — 보안 60→72.
3. **vcpkg.json 잠금파일 + CI** — 빌드 재현성 76→85.
4. **다른 장르 타이틀 1개 이상**을 같은 엔진으로 — 상용화(b) 48→60.
5. **서드파티용 "Getting Started" 문서 + 빈 프로젝트 템플릿** — 사용성 58→68.
6. (장기) **2번째 렌더 백엔드(Vulkan/DX12)·크로스플랫폼** — 가장 큰 천장.

---

## 6. 결론

- **빌드·배포: 진짜로 된다.** 원클릭 패키징 + 독립 실행 + 에셋 쿠킹까지 갖춘 건 자체엔진치고 드물게 완결적. 최고 강점(76).
- **상용 게임 출시(a): 가능** — 단일 보스전급 인디 스코프, 큐레이션 채널 한정.
- **엔진을 제품으로 상용화(b): 아직** — 크래시 리포팅·코드 서명·생태계·크로스플랫폼이 관문.
- **안전성·사용성**이 상대적 약점(56·58) — 크래시 핸들러와 서드파티 온보딩만 보완해도 70점 초반으로 상승.

**종합 64/100 (절대 상용 제품 기준) / 약 90/100 (자체엔진 포트폴리오 기준).**

---

## 부록: 코드 감사 핵심 수치

| 항목 | 수치 |
|---|---|
| 소스 파일 수 | 279개 (C++/헤더) |
| 총 LOC | 약 82,452줄 |
| 최대 파일 | `DeferredRenderSystem.cpp` (5,689줄) |
| assert/체크 | 691건 |
| try/catch | 66건 |
| 스마트포인터 | 255건 |
| raw new/delete | 41건 |
| 네트워크 코드 | 0줄 (오프라인 전용) |
| 외부 의존성 | FMOD, PhysX, Assimp, DirectXTK/Tex, RTTR, zlib/minizip 등 |

### 주요 참조 파일
- 빌드/패키징: `Build.bat`, `CMakeLists.txt`, `EngineSource/Engine/src/Editor/Tools/BuildGameWindow.cpp`
- 보안/경로검증: `Runtime/Engine/EngineInitialize.cpp`, `Runtime/Resources/ResourceManager.cpp`, `Runtime/Scripting/ScriptHotReload.cpp`
- 직렬화: `Runtime/Resources/Serialization/JsonRttr.h`

---

## Sources (비교군 출처)
- Pearl Abyss BlackSpace Engine — [Crimson Desert Dev Archives](https://crimsondesert.pearlabyss.com/en-us/News/Notice/Detail?_boardNo=40), [GDC 2025](https://www.gamespress.com/Pearl-Abyss-Showcases-BlackSpace-Engine-at-GDC-2025)
- MapleStory 초기 자체엔진(DX8) — [NamuWiki](https://en.namu.wiki/w/%EB%A9%94%EC%9D%B4%ED%94%8C%EC%8A%A4%ED%86%A0%EB%A6%AC)
- NCsoft → UE 전환 — [Throne and Liberty (Wikipedia)](https://en.wikipedia.org/wiki/Throne_and_Liberty)
- 인디 자체엔진 목록 — [raysan5/custom_game_engines](https://github.com/raysan5/custom_game_engines), [Noita](https://noitagame.com/)
- AAA 자체엔진 개요 — [TechDigest](https://www.techdigest.tv/2025/04/what-game-engine-do-aaa-video-game-studios-use.html)
- 오픈소스 C++ 엔진 비교 — [GameFromScratch](https://gamefromscratch.com/c-c-game-engines-in-2025/)

> 본 평가는 2026-06-30 기준 코드 정적 감사 + 공개 자료 비교로 작성됨. 실측 성능·런타임 안정성 테스트는 포함하지 않음.

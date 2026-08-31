# ScriptBuild 헤더 정리 & 빌드 속도 영향 정리

## 1. 목적
- 스크립트 컴포넌트(.cpp/.h)에서 `TransformComponent`와 주요 컴포넌트를 **별도 include 없이** 사용 가능하게 한다.
- 스크립트 솔루션에서 **자동완성(IDE IntelliSense)**이 잘 뜨도록 헤더 가시성을 확보한다.
- 헤더 중복/누락으로 인한 **빌드 에러**를 줄이고 **스크립트 빌드 시간**을 개선한다.

---

## 2. 문제 요약 (이전 상태)
### 2.1 스크립트에서 타입 미정의 오류
- `CameraComponent`, `TransformComponent` 등 컴포넌트 타입이 **정의되지 않은 형식**으로 인식.
- 원인: 스크립트 컴파일은 `Engine`을 직접 빌드하지 않으며 해당 타입 헤더가 스크립트 프로젝트에 포함되지 않았음.

### 2.2 DirectX 타입 사용 에러
- `DirectX::XMFLOAT3` 같은 타입이 스크립트 헤더에서 미정의.
- 원인: 스크립트 헤더(`SocketSetupExample.h`, `SoundBoxSetupExample.h`)에 `<DirectXMath.h>` 누락.

### 2.3 유지보수 부담
- `TransformComponent`를 쓰는 스크립트마다 `.cpp`에서 개별 include 필요.
- 헤더 중복/누락이 잦아져 빌드 에러 발생 확률 증가.

---

## 3. 변경 사항 (해결 방식)
### 3.1 IScript 기본 include 강화
- `Engine/src/Runtime/Scripting/IScript.h`에 `TransformComponent`를 직접 include.
- 결과: 스크립트는 `IScript.h`만 포함해도 `TransformComponent` 사용 가능.

### 3.2 스크립트 전용 PCH 도입
- `Engine/src/Runtime/Scripting/ScriptPCH.h` 추가.
- 주요 컴포넌트 헤더를 한 번에 모아서 **스크립트 프로젝트에 강제 include**.

### 3.3 CMake에서 PCH 강제 포함
- `ScriptsBuild/CMakeLists.txt`에서 `target_precompile_headers`로 PCH 적용.
- 결과: 스크립트 코드에서 `MaterialC...` 등 주요 컴포넌트를 자동완성으로 즉시 검색 가능.

### 3.4 스크립트 cpp 헤더 정리
- 스크립트 .cpp 파일의 `TransformComponent` 개별 include 제거.
- DirectX 타입 사용하는 스크립트 헤더에 `<DirectXMath.h>` 추가.

### 3.5 Debug PDB 잠금 회피
- Debug 빌드에서 `PDB_NAME_DEBUG`를 타임스탬프 기반으로 변경.
- Release 빌드만 `/DEBUG:NONE` 적용 (핫리로드 시 파일 잠금 최소화).

---

## 4. 빌드 속도 영향 (예상)
> 정량 수치는 환경/캐시/코어 수에 따라 크게 달라집니다.  
> 아래는 **일반적인 경향**입니다.

### 4.1 Clean Build (전체 빌드)
- PCH 생성 과정이 추가되므로 **첫 빌드 시간은 약간 증가**할 수 있음.

### 4.2 Incremental Build (스크립트 수정 후 재빌드)
다음 조건에서 **시간 단축 효과가 크게 나타남**:
- 수정 범위가 **스크립트 일부 파일에 국한**될 때
- 컴포넌트 헤더의 **대규모 재파싱이 줄어들 때**

**체감 가능한 변화 예시**
- 스크립트 파일이 20~50개 이상일 경우:  
  **수십 % 감소**(보통 10~40% 수준) 가능

---

## 5. 실제 측정 방법 (추천)
### 방법 A: PowerShell로 빌드 시간 측정
```powershell
Measure-Command {
  cmake --build ScriptsBuild\build --target AliceScripts --config Debug
}
```
> 같은 조건으로 2~3회 반복 실행 후 평균값 비교.

### 방법 B: Visual Studio 출력 로그
- 빌드 완료 시 **“빌드에 걸린 시간”** 확인
- 변경 전/후를 비교

---

## 6. 결과 요약
- 스크립트에서 `TransformComponent`를 **include 없이 바로 사용 가능**.
- `MaterialComponent`, `CameraComponent` 등 주요 컴포넌트도 자동완성에 즉시 노출.
- 스크립트 빌드에서 **컴파일 에러 발생률 감소**.
- **Incremental Build 속도 개선 가능성** 확보.

---

## 7. 주의사항
- PCH 변경 후에는 **ScriptsBuild 솔루션 재로딩**이 필요할 수 있음.
- PCH에 너무 많은 헤더를 넣으면 **초기 빌드 시간 증가** 가능.
- 필요 없는 컴포넌트는 PCH에서 제거해도 됨.

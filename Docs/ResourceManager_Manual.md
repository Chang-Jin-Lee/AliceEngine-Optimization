# ResourceManager 사용 매뉴얼

## 개요

`ResourceManager`는 에디터 모드와 게임 모드에서 리소스를 통일된 방식으로 로드할 수 있게 해주는 시스템입니다. 텍스처, 텍스트 파일, JSON 파일 등을 간단한 `Load<T>()` API로 로드할 수 있습니다.

**헤더 위치**: `Engine/src/Runtime/Resources/ResourceManager.h`

## 기본 사용법

### 싱글톤 접근

```cpp
#include "Runtime/Resources/ResourceManager.h"

// 싱글톤 인스턴스 가져오기
ResourceManager& rm = ResourceManager::Get();
```

### 템플릿 로드 함수

```cpp
// 기본 형태
auto resource = ResourceManager::Get().Load<Type>("논리경로", 추가인자...);
```

---

## 1. 텍스처 로드하기

### 지원 포맷
- **`.dds`** (권장) - DirectX 기본 포맷, 압축 지원
- **`.png`** - WIC를 통한 로드
- **`.jpg`, `.jpeg`** - WIC를 통한 로드
- **`.bmp`** - WIC를 통한 로드

### ️ 지원하지 않는 포맷
- **`.tga`** - 런타임에서 지원하지 않습니다. 반드시 `.dds` 또는 `.png`로 변환하세요.

### 사용 예시

```cpp
#include "Runtime/Resources/ResourceManager.h"
#include <d3d11.h>

void MyScript::LoadTexture()
{
    // ID3D11Device*가 필요합니다
    ID3D11Device* device = GetDevice(); // 엔진에서 가져오기

    // 텍스처 로드
    auto tex = ResourceManager::Get().Load<ID3D11ShaderResourceView>(
        "Resource/Textures/Character/Hero_D.png", 
        device
    );

    if (tex)
    {
        // ComPtr이므로 .Get()으로 raw pointer 접근
        ID3D11ShaderResourceView* srv = tex.Get();
        
        // 머티리얼에 설정하거나 UI에 사용
        m_material->SetAlbedoTexture(srv);
    }
    else
    {
        ALICE_LOG_ERROR("Failed to load texture!");
    }
}
```

### 반환 타입
- `Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>`
- 실패 시 `nullptr` 반환

---

## 2. 텍스트 파일 로드하기

텍스트 파일(.txt, .xml, .shader, .hlsl 등)을 로드할 때 사용합니다.

### 사용 예시

```cpp
#include "Runtime/Resources/ResourceManager.h"
#include <string>

void MyScript::LoadConfig()
{
    // 텍스트 파일 로드
    auto textPtr = ResourceManager::Get().Load<std::string>("Assets/Data/story.txt");

    if (textPtr)
    {
        // std::shared_ptr<std::string>이므로 *textPtr로 접근
        ALICE_LOG_INFO("Text Loaded: %s", textPtr->c_str());
        
        // 또는
        std::string content = *textPtr;
        ProcessText(content);
    }
}
```

### 반환 타입
- `std::shared_ptr<std::string>`
- 실패 시 `nullptr` 반환

---

## 3. JSON 파일 로드하기

JSON 파일을 파싱하여 `nlohmann::json` 객체로 로드합니다.

### 사용 예시

```cpp
#include "Runtime/Resources/ResourceManager.h"
#include "json/json.hpp" // nlohmann::json 사용 시 필요

void MyScript::LoadJsonData()
{
    // JSON 파일 로드
    auto jsonPtr = ResourceManager::Get().Load<nlohmann::json>("Assets/Data/MonsterStats.json");

    if (jsonPtr)
    {
        // std::shared_ptr<nlohmann::json>이므로 *jsonPtr로 접근
        int hp = (*jsonPtr)["hp"].get<int>();
        std::string name = (*jsonPtr)["name"].get<std::string>();
        float speed = (*jsonPtr)["speed"].get<float>();
        
        ALICE_LOG_INFO("Monster: %s, HP: %d, Speed: %.2f", name.c_str(), hp, speed);
    }
}
```

### 반환 타입
- `std::shared_ptr<nlohmann::json>`
- 실패 시 `nullptr` 반환
- 파싱 실패 시 에러 로그 출력 후 `nullptr` 반환

---

## 4. 논리 경로 규칙

### 경로 형식

ResourceManager는 다음 논리 경로를 지원합니다:

- **`Resource/...`** - 리소스 파일 (텍스처, 모델 등)
  - 예: `Resource/Textures/Character/Hero_D.png`
  - 예: `Resource/fbx/char/char.fbx`

- **`Assets/...`** - 에셋 파일 (씬, 머티리얼, 설정 등)
  - 예: `Assets/Materials/char_0.mat`
  - 예: `Assets/Scenes/Main.scene`
  - 예: `Assets/Data/MonsterStats.json`

- **`Cooked/...`** - 쿠킹된 파일 (게임 모드에서 자동 처리)
  - 일반적으로 직접 사용하지 않음

---

## 4-1. 경로 선택 가이드 (Assets vs Resource)

**요약:**  
런타임에서 “실제 데이터(텍스처/사운드/폰트/FBX 원본)”를 직접 로드하는 것은 **`Resource/...`**를 사용합니다.  
씬/프리팹/머티리얼/FBX 인스턴스 같은 “메타/에셋 정의”는 **`Assets/...`**를 사용합니다.

| 대상 | 권장 경로 | 이유 |
| --- | --- | --- |
| 텍스처 | `Resource/Textures/...` | 게임 모드에서 Cooked/Chunks로 매핑 |
| 사운드(.wav/.ogg/...) | `Resource/Sound/...` | 오디오 로더가 Resource 경로를 사용 |
| 폰트(.ttf/.ttc) | `Resource/Fonts/...` | UIFontCache/UIText가 Resource 경로를 사용 |
| FBX 원본(.fbx) | `Resource/fbx/...` | 원본 리소스는 Resource로 취급 |
| FBX 인스턴스(.fbxasset) | `Assets/Fbx/...` | 씬/프리팹에서 참조하는 메타 에셋 |
| 머티리얼(.mat) | `Assets/Materials/...` | 씬/프리팹에서 참조하는 에셋 |
| 씬(.scene) | `Assets/Scenes/...` | 씬 파일은 Assets로 관리 |
| 프리팹(.prefab) | `Assets/Prefabs/...` | 프리팹 파일은 Assets로 관리 |
| Preload.json | `Assets/Startup/Preload.json` | 프리로드 목록은 Assets에 저장 |

### 스크립트에서 스폰/로드할 때 주의

- 스킨 메시를 런타임에서 생성할 때는 **`Assets/Fbx/xxx.fbxasset` 경로를 명시**하는 방식이 가장 안전합니다.  
  (baseName만 넣는 방식은 충돌/해시 키 상황에서 실패할 수 있음)
- 동일 이름 FBX 충돌 시 `xxx_해시8자리.fbxasset` 형태로 생성될 수 있습니다.  
  이 경우에도 **`Assets/Fbx/...` 경로를 그대로 사용**하면 자동으로 올바른 키로 연결됩니다.

### 경로 해석

- **에디터 모드**: 원본 파일(`Resource/`, `Assets/`)을 직접 읽습니다.
- **게임 모드**: 
  - `Resource/...` → `Cooked/Chunks/...` (청크에서 로드)
  - `Assets/...` → `Metas/Chunks/...` (청크에서 로드)

### 게임 모드 청크 해시 규칙 (대소문자 안전)

게임 모드에서는 청크 해시를 계산할 때 **경로를 정규화**합니다.

- 슬래시 통일(`/`)
- `./` 제거
- `lexically_normal()` 적용
- **소문자 변환**

즉, `Resource/Textures/Hero_D.png`와 `resource/textures/hero_d.png`는
동일한 청크로 매핑됩니다.

**레거시 호환**:
- 정규화 해시로 청크가 없으면 **기존 해시(원 문자열)**로 폴백합니다.
- 오래된 빌드도 최대한 유지되지만 경로 케이스가 다르면 재쿠킹이 안전합니다.

---

## 5. 새로운 타입 로더 추가하기 (고급)

새로운 리소스 타입(예: 폰트, 오디오 등)을 로드하고 싶다면 `ResourceLoader` 템플릿을 특수화하면 됩니다.

### 예시: DirectX SpriteFont 로더

#### 1. 헤더에 선언 추가 (`ResourceManager.h`)

```cpp
namespace Alice {
    // DirectX::SpriteFont 특수화 선언
    template <>
    struct ResourceLoader<DirectX::SpriteFont> {
        using ReturnType = std::shared_ptr<DirectX::SpriteFont>;
        static ReturnType Load(const ResourceManager& rm, 
                               const std::filesystem::path& path, 
                               ID3D11Device* device);
    };
}
```

#### 2. 구현 추가 (`ResourceManager.cpp`)

```cpp
#include <DirectXTK/SpriteFont.h>

std::shared_ptr<DirectX::SpriteFont>
ResourceLoader<DirectX::SpriteFont>::Load(const ResourceManager& rm, 
                                          const std::filesystem::path& path, 
                                          ID3D11Device* device)
{
    if (!device)
    {
        ALICE_LOG_ERRORF("[ResourceManager] Load<SpriteFont> Error: Device is null. \"%s\"", 
            path.string().c_str());
        return nullptr;
    }

    std::vector<std::uint8_t> data;
    if (!rm.LoadBinaryAuto(path, data) || data.empty())
    {
        ALICE_LOG_ERRORF("[ResourceManager] Load<SpriteFont> Failed: File not found. \"%s\"", 
            path.string().c_str());
        return nullptr;
    }

    try
    {
        // DirectXTK SpriteFont는 메모리에서 생성 가능
        auto font = std::make_shared<DirectX::SpriteFont>(device, data.data(), data.size());
        return font;
    }
    catch (const std::exception& e)
    {
        ALICE_LOG_ERRORF("[ResourceManager] Load<SpriteFont> Error: Creation failed \"%s\" (%s)", 
            path.string().c_str(), e.what());
        return nullptr;
    }
}
```

#### 3. 사용

```cpp
auto font = ResourceManager::Get().Load<DirectX::SpriteFont>("Assets/Fonts/Arial.spritefont", device);
if (font)
{
    // 폰트 사용
    font->DrawString(spriteBatch, "Hello World", position, color);
}
```

---

## 6. 에러 처리

모든 `Load<T>()` 함수는 실패 시 `nullptr`을 반환합니다. 에러 메시지는 자동으로 로그에 출력됩니다.

### 권장 패턴

```cpp
auto resource = ResourceManager::Get().Load<Type>("path/to/resource", args...);
if (!resource)
{
    // 로드 실패 처리
    ALICE_LOG_ERROR("Failed to load resource!");
    return false; // 또는 기본값 사용
}

// 성공 시 사용
UseResource(resource);
```

---

## 7. 주의사항

### TGA 파일 처리

- `.tga` 파일은 런타임에서 지원하지 않습니다.
- 에디터에서 FBX 임포트 시 `.tga` 텍스처는 자동으로 `Resource/Textures/`에 복사되지만 런타임 로드 시 실패합니다.
- **해결 방법**: 
  1. 에셋 파이프라인에서 `.tga`를 `.dds` 또는 `.png`로 변환
  2. 또는 FBX 임포트 전에 텍스처를 미리 변환

### 경로 대소문자

- Windows에서는 대소문자를 구분하지 않지만 크로스 플랫폼을 고려하면 경로는 일관되게 작성하는 것이 좋습니다.

### 메모리 관리

- `Load<T>()`는 스마트 포인터를 반환하므로 메모리 관리가 자동으로 처리됩니다.
- 텍스처(`ComPtr`)는 참조 카운팅으로 관리됩니다.
- 텍스트/JSON(`shared_ptr`)도 참조 카운팅으로 관리됩니다.

---

## 8. 예제: 완전한 스크립트 컴포넌트

```cpp
#include "Runtime/Resources/ResourceManager.h"
#include "json/json.hpp"
#include <d3d11.h>

class MyGameScript
{
public:
    void Initialize(ID3D11Device* device)
    {
        m_device = device;
        
        // 텍스처 로드
        m_texture = ResourceManager::Get().Load<ID3D11ShaderResourceView>(
            "Resource/Textures/UI/Button.png", 
            device
        );
        
        // 설정 파일 로드
        auto config = ResourceManager::Get().Load<nlohmann::json>("Assets/Data/GameConfig.json");
        if (config)
        {
            m_maxHealth = (*config)["maxHealth"].get<int>();
            m_playerSpeed = (*config)["playerSpeed"].get<float>();
        }
        
        // 스크립트 파일 로드
        auto script = ResourceManager::Get().Load<std::string>("Assets/Scripts/MyScript.lua");
        if (script)
        {
            ExecuteScript(*script);
        }
    }
    
    void Render()
    {
        if (m_texture)
        {
            // 텍스처 사용
            m_context->PSSetShaderResources(0, 1, m_texture.GetAddressOf());
        }
    }
    
private:
    ID3D11Device* m_device = nullptr;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_texture;
    int m_maxHealth = 100;
    float m_playerSpeed = 5.0f;
};
```

---

## 요약

- **텍스처**: `Load<ID3D11ShaderResourceView>("경로", device)`
- **텍스트**: `Load<std::string>("경로")`
- **JSON**: `Load<nlohmann::json>("경로")`
- **새 타입**: `ResourceLoader<T>` 특수화로 확장 가능
- **경로**: `Resource/...` 또는 `Assets/...` 형식 사용
- **에러**: 실패 시 `nullptr` 반환, 로그에 자동 출력

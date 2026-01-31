#pragma once

#include "Runtime/Input/InputTypes.h"
#include <utility>

namespace Alice 
{
    class SceneManager;
    class ResourceManager;
    class SkinnedMeshRegistry;
    class InputSystem;

    /// 스크립트에서 사용하는 입력 API (GetKeyDown 등)
    class IScriptInput
    {
    public:
        virtual ~IScriptInput() = default;

        // --- 키보드 (기존) ---
        virtual bool GetKey(KeyCode key) const = 0;
        virtual bool GetKeyDown(KeyCode key) const = 0;
        virtual bool GetKeyUp(KeyCode key) const = 0;

        // --- 마우스 (신규 추가) ---
        // 버튼 상태 확인 (누르고 있음 / 눌림 / 떼짐)
        virtual bool GetMouseButton(MouseCode button) const = 0;
        virtual bool GetMouseButtonDown(MouseCode button) const = 0;
        virtual bool GetMouseButtonUp(MouseCode button) const = 0;

        // 마우스 현재 좌표 (Screen Space: x, y) - STL pair 활용
        virtual std::pair<float, float> GetMousePosition() const = 0;

        // 마우스 델타 (이동량) - 드래그
        virtual float GetMouseDeltaX() const = 0;
        virtual float GetMouseDeltaY() const = 0;

        // 마우스 스크롤 델타 (스크롤 이동량)
        // - 양수: 위로 스크롤, 음수: 아래로 스크롤
        virtual float GetMouseScrollDelta() const = 0;
    };

    /// 스크립트에서 사용하는 씬 전환 API (즉시 로드 대신 "요청" → 프레임 끝에 처리)
    class IScriptScene
    {
    public:
        virtual ~IScriptScene() = default;
        virtual void SwitchTo(const char* sceneName) = 0;              // 코드 씬 (SceneManager::SwitchTo)
        virtual void LoadSceneFile(const char* scenePathUtf8) = 0;      // .scene 파일 로드 (SceneFile::Load)
        virtual bool LoadSceneFileRequest(const char* scenePathUtf8) = 0; // .scene 파일 로드 요청 (SceneManager::RequestLoadSceneFile)
    };

    struct ScriptServices 
    {
        IScriptInput*        input { nullptr };
        IScriptScene*        scene { nullptr };
        SkinnedMeshRegistry* skinnedRegistry { nullptr };
        ResourceManager*     resources { nullptr };
    };
}





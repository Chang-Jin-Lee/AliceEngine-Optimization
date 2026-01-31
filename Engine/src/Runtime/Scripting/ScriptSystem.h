#pragma once

#include <string>
#include <array>

#include "Runtime/Scripting/ScriptAPI.h"
#include "Runtime/Foundation/Delegate.h"
#include "Runtime/Input/InputTypes.h"
#include <directXTK/Keyboard.h>

namespace Alice
{
    class World;
    class InputSystem;
    class SceneManager;
    class ResourceManager;
    class SkinnedMeshRegistry;

    ALICE_DECLARE_DELEGATE(FOnTrimVideoMemory);
    ALICE_DECLARE_DELEGATE(FOnAfterSceneLoaded);

    /// 모든 ScriptComponent 를 매 프레임 업데이트하는 간단한 시스템입니다.
    class ScriptSystem : public IScriptInput, public IScriptScene
    {
    public:
        void SetServices(InputSystem* input,
                         SceneManager* scenes,
                         ResourceManager* resources,
                         SkinnedMeshRegistry* skinnedRegistry);

        // Unity-style tick
        void Tick(World& world, float deltaTime);
        void PostCombatUpdate(World& world, float deltaTime);

        // 종료 시 호출
        void OnApplicationQuit(World& world);

        // === IScriptInput ===
        bool GetKey(KeyCode key) const override;
        bool GetKeyDown(KeyCode key) const override;
        bool GetKeyUp(KeyCode key) const override;

        // 마우스 함수들
        bool GetMouseButton(MouseCode button) const override;
        bool GetMouseButtonDown(MouseCode button) const override;
        bool GetMouseButtonUp(MouseCode button) const override;
        std::pair<float, float> GetMousePosition() const override;
        float GetMouseDeltaX() const override;
        float GetMouseDeltaY() const override;
        float GetMouseScrollDelta() const override;

        std::string GetResolvedPath(const char* originalPath) const;

        // === IScriptScene ===
        void SwitchTo(const char* sceneName) override;
        void LoadSceneFile(const char* scenePathUtf8) override;
        bool LoadSceneFileRequest(const char* scenePathUtf8) override;

        // 씬 요청이 남아있는지 체크 (엔진이 안전 지점에서 처리)
        bool HasPendingSceneRequests() const;
        
        // 씬 요청 커밋 (엔진이 안전 지점에서만 호출)
        void CommitSceneRequests(World& world);

        // === editormode ===
        void SetEditorMode(const bool& isEditor) { m_editorMode = isEditor; }

    private:
        static DirectX::Keyboard::Keys ToDxKey(KeyCode k);

        void BeginInputFrame();
        void EnsureServicesBound(World& world);
        void CallUpdate(World& world, float deltaTime);
        void CallLateUpdate(World& world, float deltaTime);
        void CallPostCombatUpdate(World& world, float deltaTime);
        void CallFixedUpdate(World& world, float fixedDt);
        void ProcessSceneRequests(World& world);
        bool GetKeyInternal(KeyCode key) const;
        bool GetMouseButtonInternal(MouseCode button) const;

        float m_fixedDt = 0.02f;
        float m_fixedAcc = 0.0f;

        bool m_editorMode = true;

        InputSystem* m_input = nullptr;
        SceneManager* m_scenes = nullptr;
        ResourceManager* m_resources = nullptr;
        SkinnedMeshRegistry* m_skinnedRegistry = nullptr;

        ScriptServices m_services{};

        // input snapshot (KeyCode 전체)
        std::array<bool, static_cast<std::size_t>(KeyCode::Count)> m_prevKeys{};
        std::array<bool, static_cast<std::size_t>(KeyCode::Count)> m_currKeys{};

        // input snapshot (MouseCode 전체)
        std::array<bool, static_cast<std::size_t>(MouseCode::Count)> m_prevMouseButtons{};
        std::array<bool, static_cast<std::size_t>(MouseCode::Count)> m_currMouseButtons{};

        // scene requests
        std::string m_pendingSwitch;
        std::string m_pendingSceneFile;
        
    public:
        // 씬 로드 직후 엔진 쪽에서 추가 작업
        FOnAfterSceneLoaded onAfterSceneLoaded;
        FOnTrimVideoMemory onTrimVideoMemory;
    };
}

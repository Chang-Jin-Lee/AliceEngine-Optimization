#pragma once

#include "Runtime/Input/InputTypes.h"
#include <filesystem>
#include <string>
#include <utility>

namespace DirectX
{
    struct XMFLOAT3;
}

namespace Alice 
{
    class SceneManager;
    class ResourceManager;
    class SkinnedMeshRegistry;
    class InputSystem;
    namespace Sound
    {
        enum class Type : int;
    }

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


        // 커서 표시/잠금 (UI 씬 등에서 복원용)
        virtual void SetCursorVisible(bool visible) = 0;
        virtual void SetCursorLocked(bool locked) = 0;

        // 앱 활성 상태 (Alt+Tab 등 포커스 변화 감지)
        virtual bool IsAppActive() const = 0;
        virtual bool ConsumeAppDeactivated() = 0;
        virtual bool ConsumeAppActivated() = 0;

        // Time 제어/조회 (게임 dt와 UI용 unscaled dt 분리)
        virtual void StopDeltaTime(bool stop) = 0;
        virtual bool IsDeltaTimeStopped() const = 0;
        virtual float GetGameDeltaTime() const = 0;
        virtual float GetUnscaledDeltaTime() const = 0;
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

    /// 스크립트에서 사용하는 사운드 API (엔진 SoundManager 경유)
    class IScriptAudio
    {
    public:
        virtual ~IScriptAudio() = default;
        virtual bool LoadAuto(const ResourceManager& resources,
                              const std::wstring& key,
                              const std::filesystem::path& logicalPath,
                              Sound::Type type) = 0;
        virtual void SetBGMVolume(float volume) = 0;
        virtual void PlayBGM(const std::wstring& key) = 0;
        virtual void StopBGM() = 0;
        virtual void PauseAll(bool pause) = 0;
        virtual void PauseBGM(bool pause) = 0;
        virtual void PlaySFX(const std::wstring& key, float volume, float pitch, bool loop) = 0;
        virtual void StopSfx(const std::wstring& key) = 0;
        virtual void StopAllSFX() = 0;
        virtual bool Play3D(const std::wstring& instanceId,
                            const std::wstring& key,
                            const DirectX::XMFLOAT3& pos,
                            float volume,
                            float pitch,
                            bool loop) = 0;
        virtual void Stop3D(const std::wstring& instanceId) = 0;
        virtual void Update3D(const std::wstring& instanceId,
                              const DirectX::XMFLOAT3& pos,
                              float volume,
                              float minDistance,
                              float maxDistance) = 0;
    };

    struct ScriptServices 
    {
        IScriptInput*        input { nullptr };
        IScriptScene*        scene { nullptr };
        IScriptAudio*        audio { nullptr };
        SkinnedMeshRegistry* skinnedRegistry { nullptr };
        ResourceManager*     resources { nullptr };
    };
}





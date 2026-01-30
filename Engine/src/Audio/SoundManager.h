#pragma once

#include <string>
#include <filesystem>
#include <DirectXMath.h>

namespace Alice
{
    class ResourceManager;
}

namespace Alice::Sound
{
    enum class Type
    {
        BGM,
        SFX,
        UI
    };

    bool Initialize();
    void Shutdown();
    void Update();

    bool Load(const std::wstring& key, const std::wstring& path, Type type);
    bool LoadAuto(const ResourceManager& resources,
                  const std::wstring& key,
                  const std::filesystem::path& logicalPath,
                  Type type);

    // 글로벌 설정
    void SetMasterVolume(float volume);
    void SetBGMVolume(float volume);
    void SetSFXVolume(float volume);
    void PauseAll(bool pause);

    // BGM
    void PlayBGM(const std::wstring& key, float fadeTime = 0.0f);
    void PauseBGM(bool pause);
    void StopBGM(float fadeTime = 0.0f);
    bool IsBGMPlaying();
    bool IsBGMPaused();
    bool SetBGMTimeSeconds(float sec);
    float GetBGMTimeSeconds();
    float GetBGMLengthSeconds();
    std::wstring GetCurrentBGMKey();

    // SFX (폴리포니 지원: loop=false면 중첩 재생 가능)
    // - loop=true: 핸들(ChannelID)을 반환하여 제어 가능하게 함
    // - loop=false: Fire-and-forget (중첩 재생 가능)
    void PlaySFX(const std::wstring& key, float volume = 1.0f, float pitch = 1.0f, bool loop = false);
    bool IsSfxPlaying(const std::wstring& key);
    void StopSfx(const std::wstring& key);
    void StopAllSFX();
    void StopLastSFX();
    void SetSFXVolume(float volume);
    void SetSFXPitch(float pitch);
    void SetSfxVolume(const std::wstring& key, float volume);
    void SetSfxPitch(const std::wstring& key, float pitch);

    // 3D
    void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& vel,
                     const DirectX::XMFLOAT3& forward, const DirectX::XMFLOAT3& up);
    void SetListener(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& vel,
                     const DirectX::XMVECTOR& forward, const DirectX::XMVECTOR& up);
    
    // 3D 재생 (인스턴스 ID로 제어)
    // - loop=false면 Fire-and-forget(위치 고정), loop=true면 Update3DInstance로 위치 갱신 가능
    bool Play3D(const std::wstring& instanceId, const std::wstring& key, 
                const DirectX::XMFLOAT3& pos, float volume = 1.0f, float pitch = 1.0f, bool loop = false);
    void Stop3D(const std::wstring& instanceId);
    
    // 위치/볼륨 업데이트 (Looping 3D 사운드용)
    void Update3D(const std::wstring& instanceId,
                  const DirectX::XMFLOAT3& pos,
                  float volume,
                  float minDistance,
                  float maxDistance);
    
    bool Play3DInstance(const std::wstring& instanceId, const std::wstring& soundKey, bool loop = true);
    void Stop3DInstance(const std::wstring& instanceId);
    void Update3DInstance(const std::wstring& instanceId,
                          const DirectX::XMFLOAT3& pos,
                          float volume01,
                          float minDist,
                          float maxDist);
}


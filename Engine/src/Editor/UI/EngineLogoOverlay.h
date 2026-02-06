#pragma once

#include <atomic>
#include <thread>
#include <string>
#include <wrl/client.h>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace Alice
{
    class ResourceManager;

    /// 엔진 시작 로고를 페이드 인/아웃으로 표시합니다.
    class EngineLogoOverlay
    {
    public:
        void Start(ResourceManager& resources,
            ID3D11Device* device,
            const std::string& logicalPath,
            float fadeInSec = 0.6f,
            float holdSec = 2.0f,
            float fadeOutSec = 0.6f);

        void Stop();
        void Draw();
        void SetHoldUntilRelease(bool enable);
        void RequestDismiss();

        bool IsActive() const { return m_active.load(std::memory_order_relaxed); }

    private:
        void StartTimelineThread();

        std::atomic<bool> m_active{ false };
        std::atomic<float> m_alpha{ 0.0f };
        std::atomic<bool> m_pendingStart{ false };
        std::atomic<bool> m_holdUntilRelease{ false };
        std::atomic<bool> m_releaseRequested{ false };
        std::jthread m_thread{};
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
        float m_texW = 0.0f;
        float m_texH = 0.0f;
        float m_fadeInSec = 0.6f;
        float m_holdSec = 2.0f;
        float m_fadeOutSec = 0.6f;
    };
}

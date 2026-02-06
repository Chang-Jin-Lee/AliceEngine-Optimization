#include "Editor/UI/EngineLogoOverlay.h"

#include "Runtime/Foundation/Logger.h"
#include "Runtime/Resources/ResourceManager.h"

#include "imgui.h"

#include <chrono>
#include <algorithm>
#include <d3d11.h>

namespace Alice
{
    void EngineLogoOverlay::Start(ResourceManager& resources,
        ID3D11Device* device,
        const std::string& logicalPath,
        float fadeInSec,
        float holdSec,
        float fadeOutSec)
    {
        Stop();

        if (!device)
        {
            ALICE_LOG_WARN("[EngineLogo] Start failed: device is null.");
            return;
        }

        const std::filesystem::path resolved = resources.Resolve(logicalPath);
        m_srv = resources.Load<ID3D11ShaderResourceView>(logicalPath, device);
        if (!m_srv)
        {
            ALICE_LOG_WARN("[EngineLogo] Load failed: %s (resolved: %s)",
                           logicalPath.c_str(),
                           resolved.generic_string().c_str());
            return;
        }

        m_texW = 0.0f;
        m_texH = 0.0f;
        {
            Microsoft::WRL::ComPtr<ID3D11Resource> res;
            m_srv->GetResource(res.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
            if (res && SUCCEEDED(res.As(&tex)) && tex)
            {
                D3D11_TEXTURE2D_DESC desc{};
                tex->GetDesc(&desc);
                m_texW = static_cast<float>(desc.Width);
                m_texH = static_cast<float>(desc.Height);
            }
        }

        m_fadeInSec = fadeInSec;
        m_holdSec = (std::max)(holdSec, 2.0f);
        m_fadeOutSec = fadeOutSec;

        m_active.store(true, std::memory_order_relaxed);
        m_alpha.store(0.0f, std::memory_order_relaxed);
        m_pendingStart.store(true, std::memory_order_relaxed);
        m_releaseRequested.store(false, std::memory_order_relaxed);

        ALICE_LOG_INFO("[EngineLogo] Loaded: %s (%.0fx%.0f)",
                       logicalPath.c_str(),
                       m_texW, m_texH);
    }

    void EngineLogoOverlay::Stop()
    {
        if (m_thread.joinable())
        {
            m_thread.request_stop();
            m_thread.join();
        }
        m_active.store(false, std::memory_order_relaxed);
        m_alpha.store(0.0f, std::memory_order_relaxed);
        m_pendingStart.store(false, std::memory_order_relaxed);
        m_releaseRequested.store(false, std::memory_order_relaxed);
        m_srv.Reset();
        m_texW = 0.0f;
        m_texH = 0.0f;
    }

    void EngineLogoOverlay::Draw()
    {
        if (m_pendingStart.load(std::memory_order_relaxed))
            StartTimelineThread();

        const float alpha = m_alpha.load(std::memory_order_relaxed);
        if (alpha <= 0.001f || !m_srv)
            return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
            return;

        const ImVec2 pos = viewport->Pos;
        const ImVec2 size = viewport->Size;

        ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
        if (!drawList)
            return;

        const ImVec2 maxPos(pos.x + size.x, pos.y + size.y);
        const ImU32 bg = IM_COL32(0, 0, 0, static_cast<int>(alpha * 255.0f));
        drawList->AddRectFilled(pos, maxPos, bg);

        float texW = m_texW > 0.0f ? m_texW : 512.0f;
        float texH = m_texH > 0.0f ? m_texH : 512.0f;
        const float maxW = size.x * 0.6f;
        const float maxH = size.y * 0.6f;
        const float scale = (std::min)(maxW / texW, maxH / texH);
        const float drawW = texW * scale;
        const float drawH = texH * scale;

        const ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
        const ImVec2 p0(center.x - drawW * 0.5f, center.y - drawH * 0.5f);
        const ImVec2 p1(center.x + drawW * 0.5f, center.y + drawH * 0.5f);
        const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f));
        drawList->AddImage(reinterpret_cast<ImTextureID>(m_srv.Get()), p0, p1, ImVec2(0, 0), ImVec2(1, 1), tint);
    }

    void EngineLogoOverlay::SetHoldUntilRelease(bool enable)
    {
        m_holdUntilRelease.store(enable, std::memory_order_relaxed);
    }

    void EngineLogoOverlay::RequestDismiss()
    {
        m_releaseRequested.store(true, std::memory_order_relaxed);
    }

    void EngineLogoOverlay::StartTimelineThread()
    {
        if (m_thread.joinable() || !m_active.load(std::memory_order_relaxed))
        {
            m_pendingStart.store(false, std::memory_order_relaxed);
            return;
        }

        m_pendingStart.store(false, std::memory_order_relaxed);
        const float fadeInSec = m_fadeInSec;
        const float holdSec = m_holdSec;
        const float fadeOutSec = m_fadeOutSec;

        m_thread = std::jthread([this, fadeInSec, holdSec, fadeOutSec](std::stop_token st) {
            using namespace std::chrono;
            const auto step = 16ms;

            auto UpdateAlpha = [this](float a) {
                m_alpha.store(a, std::memory_order_relaxed);
            };

            auto Fade = [&](float from, float to, float durationSec) {
                if (durationSec <= 0.0f)
                {
                    UpdateAlpha(to);
                    return;
                }
                const auto start = steady_clock::now();
                while (!st.stop_requested())
                {
                    const float t = duration_cast<duration<float>>(steady_clock::now() - start).count();
                    const float k = (std::min)(1.0f, (std::max)(0.0f, t / durationSec));
                    UpdateAlpha(from + (to - from) * k);
                    if (k >= 1.0f)
                        break;
                    std::this_thread::sleep_for(step);
                }
            };

            Fade(0.0f, 1.0f, fadeInSec);

            const auto holdEnd = steady_clock::now() + duration<float>(holdSec);
            while (!st.stop_requested() && steady_clock::now() < holdEnd)
            {
                UpdateAlpha(1.0f);
                std::this_thread::sleep_for(step);
            }

            if (m_holdUntilRelease.load(std::memory_order_relaxed))
            {
                while (!st.stop_requested() && !m_releaseRequested.load(std::memory_order_relaxed))
                {
                    UpdateAlpha(1.0f);
                    std::this_thread::sleep_for(step);
                }
            }

            Fade(1.0f, 0.0f, fadeOutSec);
            UpdateAlpha(0.0f);
            m_active.store(false, std::memory_order_relaxed);
        });
    }
}

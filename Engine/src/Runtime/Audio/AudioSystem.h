#pragma once

#include <unordered_map>
#include <string>
#include <DirectXMath.h>

#include "Runtime/ECS/World.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Audio/Components/AudioSourceComponent.h"
#include "Runtime/Audio/Components/AudioListenerComponent.h"
#include "Runtime/Audio/Components/SoundBoxComponent.h"

namespace Alice
{
    class AudioSystem
    {
    public:
        void SetResourceManager(ResourceManager* resources) { m_resources = resources; }

        void Update(World& world, double dtSec);

    private:
        // 리스너 업데이트 로직 분리 (listenerPos를 참조로 받아서 업데이트)
        void UpdateListener(World& world, DirectX::XMFLOAT3& outPos);
        struct Runtime
        {
            bool loaded{ false };
            bool started{ false };
            bool playing3D{ false };
            std::wstring key;
            std::wstring instanceId;
        };

        struct SoundBoxRuntime
        {
            bool loaded{ false };
            bool wasInside{ false };
            std::wstring key;
            std::wstring instanceId;
        };

        ResourceManager* m_resources = nullptr;
        std::unordered_map<EntityId, Runtime> m_runtime;
        std::unordered_map<EntityId, SoundBoxRuntime> m_soundBoxRuntime;
    };
}


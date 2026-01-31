#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Runtime/ECS/Entity.h"

struct aiAnimation;

namespace Alice
{
    class World;
    class SkinnedMeshRegistry;
    class AdvancedAnimator;
    struct SkinnedMeshGPU;
    struct SkinnedMeshComponent;
    struct SkinnedAnimationComponent;
    struct AdvancedAnimationComponent;

    // Advanced animation system (blend/layer/additive/IK/socket)
    class AdvancedAnimSystem
    {
    public:
        explicit AdvancedAnimSystem(SkinnedMeshRegistry& registry);

        void Update(World& world, double dtSec);

    private:
        struct Runtime
        {
            std::string meshKey;
            std::shared_ptr<SkinnedMeshGPU> mesh;
            std::unordered_map<std::string, int> clipIndexByName;
            class AdvancedAnimator* animator = nullptr;
            bool initialized = false;

            Runtime();
            Runtime(const Runtime&) = delete;
            Runtime& operator=(const Runtime&) = delete;
            Runtime(Runtime&&) noexcept;
            Runtime& operator=(Runtime&&) noexcept;
            ~Runtime();
        };

        bool EnsureRuntime(Runtime& rt,
                           const SkinnedMeshComponent& skinned,
                           const std::shared_ptr<SkinnedMeshGPU>& mesh);
        const aiAnimation* ResolveClip(const Runtime& rt, const std::string& key) const;
        float GetClipDurationSec(const aiAnimation* anim) const;
        void AdvanceTime(float& timeSec, float dtSec, float speed, float durationSec, bool loop) const;

        void ProcessAdvanced(EntityId id,
                             World& world,
                             AdvancedAnimationComponent& animComp,
                             SkinnedMeshComponent& skinned,
                             const std::shared_ptr<SkinnedMeshGPU>& mesh,
                             double dtSec);

        void ProcessSimple(EntityId id,
                           World& world,
                           SkinnedAnimationComponent& animComp,
                           SkinnedMeshComponent& skinned,
                           const std::shared_ptr<SkinnedMeshGPU>& mesh,
                           double dtSec);

        SkinnedMeshRegistry& m_registry;
        std::unordered_map<EntityId, Runtime> m_runtime;
    };
}


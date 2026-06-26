#include "Runtime/Gameplay/Sockets/SocketWorldUpdateSystem.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

#include <DirectXMath.h>

#include "Runtime/ECS/World.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Gameplay/Animation/AnimBlueprintComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/Gameplay/Sockets/SocketPoseOutputComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"

namespace Alice
{
    namespace
    {
        bool HasMeshForSockets(const SkinnedMeshRegistry& registry, const SkinnedMeshComponent* skinned)
        {
            if (!skinned || skinned->meshAssetPath.empty())
                return false;
            auto mesh = registry.Find(skinned->meshAssetPath);
            return (mesh && mesh->sourceModel);
        }

        bool LooksColumnMajor(const DirectX::XMFLOAT4X4& m)
        {
            const float rowT = std::fabs(m._41) + std::fabs(m._42) + std::fabs(m._43);
            const float colT = std::fabs(m._14) + std::fabs(m._24) + std::fabs(m._34);
            constexpr float kEps = 1e-4f;
            return (rowT <= kEps && colT > kEps);
        }

        DirectX::XMFLOAT4X4 NormalizeSocketWorld(const DirectX::XMFLOAT4X4& world)
        {
            if (!LooksColumnMajor(world))
                return world;
            DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&world);
            m = DirectX::XMMatrixTranspose(m);
            DirectX::XMFLOAT4X4 out{};
            DirectX::XMStoreFloat4x4(&out, m);
            return out;
        }

        void AddPose(std::vector<SocketPose>& poses, const std::string& name, const DirectX::XMFLOAT4X4& world)
        {
            if (name.empty())
                return;
            for (const auto& pose : poses)
            {
                if (pose.name == name)
                    return;
            }
            SocketPose p{};
            p.name = name;
            p.world = NormalizeSocketWorld(world);
            poses.push_back(std::move(p));
        }

        DirectX::XMFLOAT4X4 BuildFallbackSocketWorld(const SocketDef& s, const DirectX::XMMATRIX& ownerWorld)
        {
            using namespace DirectX;
            XMVECTOR scale = XMLoadFloat3(&s.scale);
            XMVECTOR rotation = XMLoadFloat3(&s.rotation);
            XMVECTOR translation = XMLoadFloat3(&s.position);
            XMMATRIX local =
                XMMatrixScalingFromVector(scale) *
                XMMatrixRotationRollPitchYawFromVector(rotation) *
                XMMatrixTranslationFromVector(translation);
            XMMATRIX socketWorld = local * ownerWorld;
            XMFLOAT4X4 out{};
            XMStoreFloat4x4(&out, socketWorld);
            return out;
        }
    }

    SocketWorldUpdateSystem::SocketWorldUpdateSystem(SkinnedMeshRegistry& registry)
        : m_registry(registry)
    {
    }

    void SocketWorldUpdateSystem::Update(World& world)
    {
        std::unordered_set<EntityId> owners;
        owners.reserve(world.GetComponents<SocketComponent>().size() + world.GetComponents<AdvancedAnimationComponent>().size());

        for (const auto& [id, sc] : world.GetComponents<SocketComponent>())
        {
            (void)sc;
            owners.insert(id);
        }
        for (const auto& [id, adv] : world.GetComponents<AdvancedAnimationComponent>())
        {
            (void)adv;
            owners.insert(id);
        }

        if (owners.empty())
            return;

        for (const EntityId owner : owners)
        {
            if (const auto* tr = world.GetComponent<TransformComponent>(owner); tr && !tr->enabled)
                continue;

            std::vector<SocketPose> poses;
            SocketPoseSource source = SocketPoseSource::None;

            const auto* adv = world.GetComponent<AdvancedAnimationComponent>(owner);
            const auto* animBp = world.GetComponent<AnimBlueprintComponent>(owner);
            const auto* skinned = world.GetComponent<SkinnedMeshComponent>(owner);
            const auto* anim = world.GetComponent<SkinnedAnimationComponent>(owner);
            auto* sockets = world.GetComponent<SocketComponent>(owner);

            const bool advEnabled = (adv && adv->enabled);
            const bool advActive = (advEnabled && HasMeshForSockets(m_registry, skinned));
            const bool skinnedActive = (!advEnabled && anim && HasMeshForSockets(m_registry, skinned));
            const bool animBpActive = (!advEnabled && !skinnedActive && animBp && HasMeshForSockets(m_registry, skinned));

            if (advActive)
            {
                source = SocketPoseSource::AdvancedAnim;
                for (const auto& s : adv->sockets)
                {
                    AddPose(poses, s.name, s.worldMatrix);
                    if (!s.parentBone.empty() && s.parentBone != s.name)
                        AddPose(poses, s.parentBone, s.worldMatrix);
                }
            }

            if (sockets)
            {
                if (skinnedActive)
                {
                    if (source == SocketPoseSource::None)
                        source = SocketPoseSource::SkinnedAnim;
                    for (const auto& s : sockets->sockets)
                    {
                        AddPose(poses, s.name, s.world);
                        if (!s.parentBone.empty() && s.parentBone != s.name)
                            AddPose(poses, s.parentBone, s.world);
                    }
                }
                else if (animBpActive)
                {
                    if (source == SocketPoseSource::None)
                        source = SocketPoseSource::AnimBlueprint;
                    for (const auto& s : sockets->sockets)
                    {
                        AddPose(poses, s.name, s.world);
                        if (!s.parentBone.empty() && s.parentBone != s.name)
                            AddPose(poses, s.parentBone, s.world);
                    }
                }
                else if (advActive)
                {
                    for (const auto& s : sockets->sockets)
                    {
                        AddPose(poses, s.name, s.world);
                        if (!s.parentBone.empty() && s.parentBone != s.name)
                            AddPose(poses, s.parentBone, s.world);
                    }
                }
                else if (source == SocketPoseSource::None)
                {
                    // No animation path this frame -> static fallback using owner world.
                    source = SocketPoseSource::FallbackLocal;
                    DirectX::XMMATRIX ownerWorld = DirectX::XMMatrixIdentity();
                    if (world.GetComponent<TransformComponent>(owner))
                        ownerWorld = world.ComputeWorldMatrix(owner);

                    for (const auto& s : sockets->sockets)
                    {
                        const DirectX::XMFLOAT4X4 worldM = BuildFallbackSocketWorld(s, ownerWorld);
                        AddPose(poses, s.name, worldM);
                        if (!s.parentBone.empty() && s.parentBone != s.name)
                            AddPose(poses, s.parentBone, worldM);
                    }
                }
            }

            auto* out = world.GetComponent<SocketPoseOutputComponent>(owner);
            if (poses.empty())
            {
                if (out)
                {
                    out->poses.clear();
                    out->source = SocketPoseSource::None;
                }
                continue;
            }

            if (!out)
                out = &world.AddComponent<SocketPoseOutputComponent>(owner);

            out->source = source;
            out->poses = std::move(poses);

            // Keep SocketComponent world cache in sync when possible.
            if (sockets)
            {
                for (auto& s : sockets->sockets)
                {
                    auto applyMatch = [&](const std::string& key)
                    {
                        for (const auto& pose : out->poses)
                        {
                            if (pose.name == key)
                            {
                                s.world = pose.world;
                                return true;
                            }
                        }
                        return false;
                    };

                    if (!s.name.empty())
                    {
                        if (applyMatch(s.name))
                            continue;
                    }
                    if (!s.parentBone.empty())
                    {
                        applyMatch(s.parentBone);
                    }
                }
            }
        }
    }
}

#include "Runtime/Engine/AdvancedAnimSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include <DirectXMath.h>
#include <assimp/scene.h>

#include "Runtime/Gameplay/Animation/AdvancedAnimator.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Importing/FbxModel.h"

namespace Alice
{
    namespace
    {

		DirectX::XMMATRIX BuildWorldMatrix(const TransformComponent& t)
		{
			using namespace DirectX;
			XMMATRIX S = XMMatrixScaling(t.scale.x, t.scale.y, t.scale.z);
			XMMATRIX R = XMMatrixRotationRollPitchYaw(t.rotation.x, t.rotation.y, t.rotation.z);
			XMMATRIX T = XMMatrixTranslation(t.position.x, t.position.y, t.position.z);
			return S * R * T;
		}

        bool TryParseIndex(const std::string& key, int& outIdx)
        {
            if (key.empty()) return false;
            for (char c : key)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    return false;
            }
            outIdx = std::atoi(key.c_str());
            return true;
        }

        void FireAnimNotifiesForAdvance(AdvancedAnimationComponent& animComp,
            const std::string& clipName,
            float prevTime,
            float currTime,
            float durationSec,
            bool loop,
            float deltaTime)
        {
            if (clipName.empty() || durationSec <= 0.0f)
                return;

            if (prevTime == currTime)
                return;

            if (!loop || deltaTime == 0.0f)
            {
                animComp.CheckAndFireNotifiesClamped(clipName, prevTime, currTime, durationSec);
                return;
            }

            // Handle wrap-around for looping clips (forward/backward).
            if (deltaTime > 0.0f)
            {
                if (prevTime <= currTime)
                {
                    animComp.CheckAndFireNotifiesClamped(clipName, prevTime, currTime, durationSec);
                }
                else
                {
                    // Wrapped forward: [prev -> end], then [0 -> curr]
                    animComp.CheckAndFireNotifiesClamped(clipName, prevTime, durationSec, durationSec);
                    animComp.CheckAndFireNotifiesClamped(clipName, 0.0f, currTime, durationSec);
                }
            }
            else
            {
                if (prevTime >= currTime)
                {
                    animComp.CheckAndFireNotifiesClamped(clipName, prevTime, currTime, durationSec);
                }
                else
                {
                    // Wrapped backward: [prev -> 0], then [end -> curr]
                    animComp.CheckAndFireNotifiesClamped(clipName, prevTime, 0.0f, durationSec);
                    animComp.CheckAndFireNotifiesClamped(clipName, durationSec, currTime, durationSec);
                }
            }
        }
    }

    AdvancedAnimSystem::Runtime::Runtime()
        : animator(new AdvancedAnimator())
    {
    }

    AdvancedAnimSystem::Runtime::Runtime(Runtime&& other) noexcept
    {
        meshKey = std::move(other.meshKey);
        mesh = std::move(other.mesh);
        clipIndexByName = std::move(other.clipIndexByName);
        animator = other.animator;
        initialized = other.initialized;
        other.animator = nullptr;
        other.initialized = false;
    }

    AdvancedAnimSystem::Runtime& AdvancedAnimSystem::Runtime::operator=(Runtime&& other) noexcept
    {
        if (this == &other) return *this;
        delete animator;
        meshKey = std::move(other.meshKey);
        mesh = std::move(other.mesh);
        clipIndexByName = std::move(other.clipIndexByName);
        animator = other.animator;
        initialized = other.initialized;
        other.animator = nullptr;
        other.initialized = false;
        return *this;
    }

    AdvancedAnimSystem::Runtime::~Runtime()
    {
        delete animator;
        animator = nullptr;
    }

    AdvancedAnimSystem::AdvancedAnimSystem(SkinnedMeshRegistry& registry)
        : m_registry(registry)
    {
    }

    void AdvancedAnimSystem::Update(World& world, double dtSec)
    {
        // ------------------------------
        // Advanced Animation Only
        // ------------------------------
        // 고급 애니메이션 컴포넌트가 있는 엔티티만 처리합니다.
        // 일반 애니메이션(SkinnedAnimationComponent)은 엔진의 기본 시스템이 담당합니다.
        for (auto&& [entityId, animComp] : world.GetComponents<AdvancedAnimationComponent>())
        {
            // 비활성화 상태면 스킵
            if (!animComp.enabled)
                continue;
            if (const auto* tr = world.GetComponent<TransformComponent>(entityId); tr && !tr->enabled)
                continue;

            auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId);
            if (!skinned || skinned->meshAssetPath.empty())
                continue;

            auto mesh = m_registry.Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                continue;

            ProcessAdvanced(entityId, world, animComp, *skinned, mesh, dtSec);
        }

    }

    bool AdvancedAnimSystem::EnsureRuntime(Runtime& rt,
                                           const SkinnedMeshComponent& skinned,
                                           const std::shared_ptr<SkinnedMeshGPU>& mesh)
    {
        if (!mesh || !mesh->sourceModel)
            return false;

        if (rt.initialized && rt.meshKey == skinned.meshAssetPath)
            return true;

        rt.meshKey = skinned.meshAssetPath;
        rt.mesh = mesh;
        rt.clipIndexByName.clear();

        const auto& names = mesh->sourceModel->GetAnimationNames();
        const aiScene* scene = mesh->sourceModel->GetScenePtr();

        const size_t clipCount = scene ? scene->mNumAnimations : names.size();
        for (size_t i = 0; i < clipCount; ++i)
        {
            std::string key;
            if (i < names.size())
                key = names[i];
            if (key.empty() && scene && i < scene->mNumAnimations)
            {
                const aiAnimation* a = scene->mAnimations[i];
                key = (a && a->mName.length > 0) ? std::string(a->mName.C_Str())
                                                 : ("Anim" + std::to_string(i));
            }
            if (key.empty())
                key = "Anim" + std::to_string(i);

            rt.clipIndexByName[key] = static_cast<int>(i);
        }

        if (rt.animator)
        {
            rt.animator->Initialize(
                mesh->sourceModel->GetScenePtr(),
                mesh->sourceModel->GetNodeIndexOfName(),
                mesh->sourceModel->GetGlobalInverse(),
                mesh->sourceModel->GetBoneNames(),
                mesh->sourceModel->GetBoneOffsets());
        }

        rt.initialized = true;
        return true;
    }

    const aiAnimation* AdvancedAnimSystem::ResolveClip(const Runtime& rt, const std::string& key) const
    {
        if (!rt.mesh || !rt.mesh->sourceModel)
            return nullptr;

        if (key.empty())
            return nullptr;

        const aiScene* scene = rt.mesh->sourceModel->GetScenePtr();
        if (!scene)
            return nullptr;

        if (auto it = rt.clipIndexByName.find(key); it != rt.clipIndexByName.end())
        {
            const int idx = it->second;
            if (idx >= 0 && (unsigned)idx < scene->mNumAnimations)
                return scene->mAnimations[idx];
        }

        int idx = -1;
        if (TryParseIndex(key, idx))
        {
            if (idx >= 0 && (unsigned)idx < scene->mNumAnimations)
                return scene->mAnimations[idx];
        }

        return nullptr;
    }

    float AdvancedAnimSystem::GetClipDurationSec(const aiAnimation* anim) const
    {
        if (!anim)
            return 0.0f;
        const double tps = (anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;
        if (tps <= 0.0)
            return 0.0f;
        return static_cast<float>(anim->mDuration / tps);
    }

    void AdvancedAnimSystem::AdvanceTime(float& timeSec,
                                         float dtSec,
                                         float speed,
                                         float durationSec,
                                         bool loop) const
    {
        if (durationSec <= 0.0f)
            return;

        timeSec += dtSec * speed;
        if (loop)
        {
            timeSec = std::fmod(timeSec, durationSec);
            if (timeSec < 0.0f)
                timeSec += durationSec;
        }
        else
        {
            timeSec = std::clamp(timeSec, 0.0f, durationSec);
        }
    }

    void AdvancedAnimSystem::ProcessAdvanced(EntityId id,
                                             World& world,
                                             AdvancedAnimationComponent& animComp,
                                             SkinnedMeshComponent& skinned,
                                             const std::shared_ptr<SkinnedMeshGPU>& mesh,
                                             double dtSec)
    {
        Runtime& rt = m_runtime[id];
        if (!EnsureRuntime(rt, skinned, mesh))
            return;

        // ------------------------------------------------------
        // 본 정보 캐싱 (최초 1회 혹은 변경 시)
        // ------------------------------------------------------
        if (mesh->sourceModel)
        {
            const auto& boneNames = mesh->sourceModel->GetBoneNames();
            const auto& offsets = mesh->sourceModel->GetBoneOffsets();

            const void* modelPtr = mesh->sourceModel.get();
            const bool cacheMismatch =
                (animComp.boneCacheMeshKey != skinned.meshAssetPath) ||
                (animComp.boneCacheModelPtr != modelPtr) ||
                (animComp.boneToIndex.size() != boneNames.size()) ||
                (animComp.inverseBindMatrices.size() != offsets.size()) ||
                (animComp.parentIndices.size() != boneNames.size());

            if (cacheMismatch)
            {
                animComp.boneCacheMeshKey = skinned.meshAssetPath;
                animComp.boneCacheModelPtr = modelPtr;
                animComp.boneToIndex.clear();
                animComp.parentIndices.clear();
                animComp.inverseBindMatrices.clear();
                animComp.boneGlobals.clear();

                // 1. 본 이름 -> 본 인덱스 맵
                animComp.boneToIndex.reserve(boneNames.size());
                for (size_t i = 0; i < boneNames.size(); ++i)
                {
                    animComp.boneToIndex[boneNames[i]] = static_cast<int>(i);
                }

                // 2. 역 바인드 행렬 (InvBind) -> Row-Major 캐싱
                animComp.inverseBindMatrices.resize(offsets.size());
                for (size_t i = 0; i < offsets.size(); ++i)
                {
                    DirectX::XMMATRIX offCol = DirectX::XMLoadFloat4x4(&offsets[i]);
                    DirectX::XMMATRIX offRow = DirectX::XMMatrixTranspose(offCol);
                    DirectX::XMStoreFloat4x4(&animComp.inverseBindMatrices[i], offRow);
                }

                // 3. GlobalInverse -> Row-Major 캐싱
                {
                    DirectX::XMMATRIX giCol = DirectX::XMLoadFloat4x4(&mesh->sourceModel->GetGlobalInverse());
                    DirectX::XMMATRIX giRow = DirectX::XMMatrixTranspose(giCol);
                    DirectX::XMStoreFloat4x4(&animComp.globalInverseRow, giRow);
                }

                // 4. 부모 인덱스 (Hierarchy)
                const auto& skeleton = mesh->sourceModel->GetSkeleton();
                animComp.parentIndices.resize(boneNames.size(), -1);

                // 본 이름 -> 인덱스 맵 생성 (본만 필터링)
                std::unordered_map<std::string, int> boneNameToIndex;
                boneNameToIndex.reserve(boneNames.size());
                for (size_t i = 0; i < boneNames.size(); ++i)
                {
                    boneNameToIndex[boneNames[i]] = static_cast<int>(i);
                }

                // 스켈레톤 노드에서 본의 부모 인덱스 찾기
                for (size_t i = 0; i < skeleton.size(); ++i)
                {
                    const auto& node = skeleton[i];
                    if (!node.isBone) continue;

                    // 본 이름으로 본 인덱스 찾기
                    auto it = boneNameToIndex.find(node.name);
                    if (it == boneNameToIndex.end()) continue;

                    int boneIdx = it->second;

                    // 부모가 본인 경우에만 부모 인덱스 설정
                    if (node.parent >= 0 && node.parent < (int)skeleton.size())
                    {
                        const auto& parentNode = skeleton[node.parent];
                        if (parentNode.isBone)
                        {
                            auto parentIt = boneNameToIndex.find(parentNode.name);
                            if (parentIt != boneNameToIndex.end())
                            {
                                animComp.parentIndices[boneIdx] = parentIt->second;
                            }
                        }
                    }
                }
            }
        }

        const aiAnimation* baseA = ResolveClip(rt, animComp.base.clipA);
        const aiAnimation* baseB = ResolveClip(rt, animComp.base.clipB);
        const aiAnimation* upperA = ResolveClip(rt, animComp.upper.clipA);
        const aiAnimation* upperB = ResolveClip(rt, animComp.upper.clipB);
        const aiAnimation* additiveA = ResolveClip(rt, animComp.additive.clip);
        const aiAnimation* additiveRef = ResolveClip(rt, animComp.additive.refClip);

        // ------------------------------
        // Time advance & Notify Check
        // ------------------------------
        if (animComp.playing)
        {
            // [Base Layer Notify 체크]
            if (animComp.base.autoAdvance && baseA)
            {
                float prevTime = animComp.base.timeA;
                const float dur = GetClipDurationSec(baseA);
                
                // 시간 진행
                AdvanceTime(animComp.base.timeA, (float)dtSec, animComp.base.speedA, dur, animComp.base.loopA);
                
                // 노티파이 실행 (현재 시간이 바뀌었으므로 체크)
                FireAnimNotifiesForAdvance(
                    animComp,
                    animComp.base.clipA,
                    prevTime,
                    animComp.base.timeA,
                    dur,
                    animComp.base.loopA,
                    static_cast<float>(dtSec) * animComp.base.speedA);
            }

            if (animComp.base.autoAdvance && baseB)
            {
                float prevTime = animComp.base.timeB;
                const float dur = GetClipDurationSec(baseB);
                AdvanceTime(animComp.base.timeB, (float)dtSec, animComp.base.speedB, dur, animComp.base.loopB);
                if (!animComp.base.clipB.empty() && animComp.base.clipB != animComp.base.clipA)
                {
                    FireAnimNotifiesForAdvance(
                        animComp,
                        animComp.base.clipB,
                        prevTime,
                        animComp.base.timeB,
                        dur,
                        animComp.base.loopB,
                        static_cast<float>(dtSec) * animComp.base.speedB);
                }
            }

            if (animComp.upper.autoAdvance && upperA)
            {
                float prevTime = animComp.upper.timeA;
                const float dur = GetClipDurationSec(upperA);
                AdvanceTime(animComp.upper.timeA, (float)dtSec, animComp.upper.speedA, dur, animComp.upper.loopA);
                FireAnimNotifiesForAdvance(
                    animComp,
                    animComp.upper.clipA,
                    prevTime,
                    animComp.upper.timeA,
                    dur,
                    animComp.upper.loopA,
                    static_cast<float>(dtSec) * animComp.upper.speedA);
            }

            if (animComp.upper.autoAdvance && upperB)
            {
                float prevTime = animComp.upper.timeB;
                const float dur = GetClipDurationSec(upperB);
                AdvanceTime(animComp.upper.timeB, (float)dtSec, animComp.upper.speedB, dur, animComp.upper.loopB);
                if (!animComp.upper.clipB.empty() && animComp.upper.clipB != animComp.upper.clipA)
                {
                    FireAnimNotifiesForAdvance(
                        animComp,
                        animComp.upper.clipB,
                        prevTime,
                        animComp.upper.timeB,
                        dur,
                        animComp.upper.loopB,
                        static_cast<float>(dtSec) * animComp.upper.speedB);
                }
            }

            if (animComp.additive.autoAdvance && additiveA)
            {
                float prevTime = animComp.additive.time;
                const float dur = GetClipDurationSec(additiveA);
                AdvanceTime(animComp.additive.time, (float)dtSec, animComp.additive.speed, dur, animComp.additive.loop);
                FireAnimNotifiesForAdvance(
                    animComp,
                    animComp.additive.clip,
                    prevTime,
                    animComp.additive.time,
                    dur,
                    animComp.additive.loop,
                    static_cast<float>(dtSec) * animComp.additive.speed);
            }

            if (animComp.procedural.strength > 0.0f)
                animComp.procedural.timeSec += (float)dtSec;
        }

        if (animComp.base.clipB.empty() || animComp.base.clipB == animComp.base.clipA)
            animComp.base.timeB = animComp.base.timeA;

        if (animComp.upper.clipB.empty() || animComp.upper.clipB == animComp.upper.clipA)
            animComp.upper.timeB = animComp.upper.timeA;

        // ------------------------------
        // Build update desc
        // ------------------------------
        AdvancedAnimator::UpdateDesc d{};
        d.dt = (float)dtSec;

        // Always evaluate base to keep bind pose when channels are missing
        d.base.enabled = true;
        d.base.animA = baseA;
        d.base.timeA = animComp.base.timeA;
        d.base.animB = baseB;
        d.base.timeB = animComp.base.timeB;
        d.base.blend01 = animComp.base.blend01;
        d.base.layerAlpha = 1.0f;

        d.upper.enabled = animComp.upper.enabled;
        d.upper.animA = upperA;
        d.upper.timeA = animComp.upper.timeA;
        d.upper.animB = upperB;
        d.upper.timeB = animComp.upper.timeB;
        d.upper.blend01 = animComp.upper.blend01;
        d.upper.layerAlpha = animComp.upper.layerAlpha;

        d.additive.enabled = animComp.additive.enabled;
        d.additive.anim = additiveA;
        d.additive.time = animComp.additive.time;
        d.additive.ref = additiveRef;
        d.additive.alpha = animComp.additive.alpha;

        d.procedural.strength = animComp.procedural.strength;
        d.procedural.seed = animComp.procedural.seed;
        d.procedural.timeSec = animComp.procedural.timeSec;

        // 다중 IK 체인 처리
        d.ikChains.clear();
        for (const auto& ikChain : animComp.ikChains)
        {
            if (!ikChain.enabled || ikChain.tipBone.empty())
                continue;
                
            AdvancedAnimator::IKDesc ikDesc{};
            ikDesc.enabled = true;
            ikDesc.tipBone = ikChain.tipBone.c_str();
            ikDesc.chainLen = ikChain.chainLength;
            ikDesc.targetMS = DirectX::XMLoadFloat3(&ikChain.targetMS);
            ikDesc.weight = ikChain.weight;
            d.ikChains.push_back(ikDesc);
        }
        
        // 기존 단일 IK 처리 (ikChains가 비어있을 때만)
        if (d.ikChains.empty())
        {
            d.ik.enabled = animComp.ik.enabled;
            d.ik.tipBone = animComp.ik.tipBone.empty() ? nullptr : animComp.ik.tipBone.c_str();
            d.ik.chainLen = animComp.ik.chainLength;
            d.ik.targetMS = DirectX::XMLoadFloat3(&animComp.ik.targetMS);
            d.ik.weight = animComp.ik.weight;
        }
        else
        {
            // ikChains가 있으면 기존 ik는 비활성화
            d.ik.enabled = false;
        }

        d.aim.enabled = animComp.aim.enabled;
        d.aim.yawRad = animComp.aim.yawRad;
        d.aim.weight = animComp.aim.weight;

        // ------------------------------
        // Socket definitions (push to runtime)
        // ------------------------------
        for (const auto& s : animComp.sockets)
        {
            rt.animator->SetSocketSRT(s.name, s.parentBone, s.pos, s.rotDeg, s.scale);
        }

        // ------------------------------
        // Evaluate
        // ------------------------------
        rt.animator->Update(d);

        // ------------------------------
        // Bone global cache (row-major)
        // ------------------------------
        if (mesh->sourceModel)
        {
            const auto& boneNames = mesh->sourceModel->GetBoneNames();
            if (!boneNames.empty())
            {
                animComp.boneGlobals.resize(boneNames.size());
                static const DirectX::XMFLOAT4X4 s_identity{
                    1,0,0,0,
                    0,1,0,0,
                    0,0,1,0,
                    0,0,0,1
                };

                for (size_t i = 0; i < boneNames.size(); ++i)
                {
                    DirectX::XMMATRIX boneGlobalRow;
                    if (rt.animator->GetBoneGlobalMatrix(boneNames[i], boneGlobalRow))
                    {
                        DirectX::XMStoreFloat4x4(&animComp.boneGlobals[i], boneGlobalRow);
                    }
                    else
                    {
                        animComp.boneGlobals[i] = s_identity;
                    }
                }
            }
            else
            {
                animComp.boneGlobals.clear();
            }
        }

        // ------------------------------
        // Palette output
        // ------------------------------
        // AdvancedAnimator의 finalTransforms는 Column-Major 형식이므로
        // Row-Major로 변환하여 저장 (렌더링 시스템에서 GPU 업로드 시 다시 전치됨)
        const auto& finals = rt.animator->GetFinalTransforms();
        if (finals.empty())
        {
            // 본 행렬이 없으면 bind pose (identity) 사용
            static DirectX::XMFLOAT4X4 s_identityBone = DirectX::XMFLOAT4X4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
            skinned.boneMatrices = &s_identityBone;
            skinned.boneCount = 1;
            return;
        }

        animComp.palette.resize(finals.size());
        for (size_t i = 0; i < finals.size(); ++i)
        {
            // AdvancedAnimator는 Column-Major 행렬을 생성하므로
            // Row-Major로 변환하여 저장 (렌더링 시스템에서 GPU 업로드 시 전치 적용)
            DirectX::XMMATRIX rowMajor = DirectX::XMMatrixTranspose(finals[i]);
            DirectX::XMStoreFloat4x4(&animComp.palette[i], rowMajor);
        }

        skinned.boneMatrices = animComp.palette.data();
        skinned.boneCount = static_cast<std::uint32_t>(animComp.palette.size());

        // ------------------------------
        // Socket world outputs (엔진 로우 컨벤션)
        // ------------------------------
        DirectX::XMMATRIX charWorld = DirectX::XMMatrixIdentity();
		if (const auto* t = world.GetComponent<TransformComponent>(id))
			charWorld = BuildWorldMatrix(*t);
        DirectX::XMMATRIX charWorldRow = charWorld;

        for (auto& s : animComp.sockets)
        {
            DirectX::XMMATRIX localRow =
                DirectX::XMMatrixScaling(s.scale.x, s.scale.y, s.scale.z) *
                DirectX::XMMatrixRotationRollPitchYaw(
                    DirectX::XMConvertToRadians(s.rotDeg.x),
                    DirectX::XMConvertToRadians(s.rotDeg.y),
                    DirectX::XMConvertToRadians(s.rotDeg.z)) *
                DirectX::XMMatrixTranslation(s.pos.x, s.pos.y, s.pos.z);

            DirectX::XMMATRIX socketWorld = localRow * charWorldRow;
            if (!s.parentBone.empty())
            {
                DirectX::XMMATRIX boneGlobalRow;
                if (rt.animator->GetBoneGlobalMatrix(s.parentBone, boneGlobalRow))
                {
                    socketWorld = localRow * boneGlobalRow * charWorldRow;
                }
            }

            DirectX::XMStoreFloat4x4(&s.worldMatrix, socketWorld);
        }

        // ------------------------------
        // SocketComponent.sockets[].world 갱신 (스크립트/에디터로 추가한 소켓, 로우 컨벤션)
        // ------------------------------
        if (auto* socketComp = world.GetComponent<SocketComponent>(id))
        {
            for (auto& s : socketComp->sockets)
            {
                DirectX::XMMATRIX boneGlobalRow;
                if (!rt.animator->GetBoneGlobalMatrix(s.parentBone, boneGlobalRow))
                    continue;

                DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&s.scale);
                DirectX::XMVECTOR rotation = DirectX::XMLoadFloat3(&s.rotation);
                DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&s.position);
                DirectX::XMMATRIX localRow =
                    DirectX::XMMatrixScalingFromVector(scale) *
                    DirectX::XMMatrixRotationRollPitchYawFromVector(rotation) *
                    DirectX::XMMatrixTranslationFromVector(translation);

                DirectX::XMMATRIX socketWorld = localRow * boneGlobalRow * charWorldRow;
                DirectX::XMStoreFloat4x4(&s.local, localRow);
                DirectX::XMStoreFloat4x4(&s.world, socketWorld);
            }
        }
    }

    void AdvancedAnimSystem::ProcessSimple(EntityId id,
                                           World& world,
                                           SkinnedAnimationComponent& animComp,
                                           SkinnedMeshComponent& skinned,
                                           const std::shared_ptr<SkinnedMeshGPU>& mesh,
                                           double dtSec)
    {
        (void)world; // 미사용 파라미터 경고 방지

        // 1. 런타임 초기화 확인
        Runtime& rt = m_runtime[id];
        if (!EnsureRuntime(rt, skinned, mesh))
            return;

        // 2. 애니메이션 클립 유효성 확인
        const aiScene* scene = mesh->sourceModel->GetScenePtr();
        if (!scene || scene->mNumAnimations == 0)
            return;

        int clipIdx = animComp.clipIndex;
        if (clipIdx < 0)
            clipIdx = 0;
        if ((unsigned)clipIdx >= scene->mNumAnimations)
            clipIdx = (int)scene->mNumAnimations - 1;
        
        // 인덱스 갱신
        animComp.clipIndex = clipIdx;

        const aiAnimation* anim = scene->mAnimations[clipIdx];
        if (!anim)
            return;

        // 3. 시간 업데이트 (재생 중일 때만)
        if (animComp.playing)
        {
            const float dur = GetClipDurationSec(anim);
            float time = static_cast<float>(animComp.timeSec);
            AdvanceTime(time, (float)dtSec, animComp.speed, dur, true); // Loop true
            animComp.timeSec = static_cast<double>(time);
        }

        // 4. 애니메이터 업데이트 (Simple Mode)
        // AdvancedAnimator 내부에서 EvaluateLikeFbxAnimation을 호출하도록 유도
        // (blend01 = 0.0f, animA == animB 이면 내부적으로 단일 클립 평가로 빠짐)
        AdvancedAnimator::UpdateDesc d{};
        d.dt = (float)dtSec;
        d.base.enabled = true;
        d.base.animA = anim;
        d.base.timeA = (float)animComp.timeSec;
        d.base.animB = anim;               // A와 B를 같게 설정
        d.base.timeB = (float)animComp.timeSec;
        d.base.blend01 = 0.0f;             // 블렌딩 없음

        rt.animator->Update(d);

        // 5. 결과 행렬 가져오기
        const auto& finals = rt.animator->GetFinalTransforms();
        
        // 본 데이터가 없는 경우 (Bone이 없는 노드 애니메이션 등)
        if (finals.empty())
        {
            static DirectX::XMFLOAT4X4 s_identityBone(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
            skinned.boneMatrices = &s_identityBone;
            skinned.boneCount = 1;
            return;
        }

        // 6. GPU 전송을 위해 포맷 변환 (Column-Major -> Row-Major)
        // 메모리 재할당 최소화
        if (animComp.palette.size() != finals.size())
        {
            animComp.palette.resize(finals.size());
        }

        // 람다 없이 직접 루프 사용
        size_t count = finals.size();
        for (size_t i = 0; i < count; ++i)
        {
            DirectX::XMMATRIX rowMajor = DirectX::XMMatrixTranspose(finals[i]);
            DirectX::XMStoreFloat4x4(&animComp.palette[i], rowMajor);
        }

        // 컴포넌트에 데이터 연결
        skinned.boneMatrices = animComp.palette.data();
        skinned.boneCount = static_cast<std::uint32_t>(animComp.palette.size());
    }
}


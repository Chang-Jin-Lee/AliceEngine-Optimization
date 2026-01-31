#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

#include <DirectXMath.h>

#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Gameplay/Animation/AnimBlueprintComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
//#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Animation/AnimBlueprintAsset.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Importing/FbxAnimation.h"
#include "Runtime/Importing/FbxModel.h"

namespace Alice
{
    /// AnimBlueprint(FSM) -> 팔레트 계산 -> SkinnedMeshComponent 연결
    class AnimBlueprintSystem
    {
    public:
        explicit AnimBlueprintSystem(SkinnedMeshRegistry& registry)
            : m_registry(registry)
        {
        }

        void SetResourceManager(ResourceManager* resources) { m_resources = resources; }

        void Update(World& world, double dtSec)
        {
            if (!m_resources) return;
            auto skinnedMap = world.GetComponents<SkinnedMeshComponent>();
            if (skinnedMap.empty())
                return;

            for (const auto& [entityId, skinned] : skinnedMap)
            {
                if (const auto* tr = world.GetComponent<TransformComponent>(entityId); tr && !tr->enabled)
                    continue;

                auto* bpComp = world.GetComponent<AnimBlueprintComponent>(entityId);
                if (!bpComp || bpComp->blueprintPath.empty())
                    continue;
                if (world.GetComponent<AdvancedAnimationComponent>(entityId))
                    continue;

                const auto mesh = m_registry.Find(skinned.meshAssetPath);
                if (!mesh || !mesh->sourceModel)
                    continue;

                Runtime& rt = m_runtime[entityId];

                if (rt.meshKey != skinned.meshAssetPath)
                {
                    rt = Runtime{};
                    rt.meshKey = skinned.meshAssetPath;
                    rt.anim.InitMetadata(mesh->sourceModel->GetScenePtr());
                    rt.anim.SetSharedContext(
                        mesh->sourceModel->GetScenePtr(),
                        mesh->sourceModel->GetNodeIndexOfName(),
                        &mesh->sourceModel->GetBoneNames(),
                        &mesh->sourceModel->GetBoneOffsets(),
                        &mesh->sourceModel->GetGlobalInverse());

                    const auto t = mesh->sourceModel->GetCurrentAnimationType();
                    if (t == FbxModel::AnimationType::Rigid) rt.anim.SetType(FbxAnimation::AnimType::Rigid);
                    else if (t == FbxModel::AnimationType::Skinned) rt.anim.SetType(FbxAnimation::AnimType::Skinned);
                    else rt.anim.SetType(FbxAnimation::AnimType::None);

                    const auto& boneNames = mesh->sourceModel->GetBoneNames();
                    for (size_t i = 0; i < boneNames.size(); ++i)
                        rt.boneNameToIndex[boneNames[i]] = i;
                }

                if (rt.blueprintPath != bpComp->blueprintPath || !rt.assetValid)
                {
                    rt.blueprintPath = bpComp->blueprintPath;
                    rt.assetValid = LoadAnimBlueprintAssetAuto(*m_resources, rt.blueprintPath, rt.asset);
                    rt.currentState = -1;
                    rt.nextState = -1;
                    rt.inTransition = false;
                    rt.timeA = 0.0;
                    rt.timeB = 0.0;
                }

                if (!rt.assetValid || rt.asset.states.empty())
                    continue;

                // 파라미터 동기화 (기본값 생성)
                EnsureParams(rt, *bpComp);

                // 상태 결정
                if (rt.currentState < 0)
                    rt.currentState = 0;

                // 상태 업데이트
                StepFSM(rt, *bpComp, dtSec);

                // 클립 매핑 업데이트
                EnsureClipMap(rt, *mesh->sourceModel);

                // 팔레트 계산
                if (!BuildPalette(rt, *bpComp))
                    continue;

                // SkinnedAnimationComponent에 결과 연결
                auto* animComp = world.GetComponent<SkinnedAnimationComponent>(entityId);
                if (!animComp)
                    animComp = &world.AddComponent<SkinnedAnimationComponent>(entityId);

                animComp->palette = rt.blended;
                if (auto* skinnedWrite = world.GetComponent<SkinnedMeshComponent>(entityId))
                {
                    skinnedWrite->boneMatrices = animComp->palette.data();
                    skinnedWrite->boneCount = static_cast<std::uint32_t>(animComp->palette.size());
                }

                // 소켓 갱신
                UpdateSockets(world, entityId, rt);
            }
        }

    private:
        struct Runtime
        {
            std::string meshKey;
            std::string blueprintPath;
            AnimBlueprintAsset asset;
            bool assetValid{ false };

            int currentState{ -1 };
            int nextState{ -1 };
            bool inTransition{ false };
            bool canInterrupt{ true };
            float transitionTime{ 0.0f };
            float transitionDuration{ 0.0f };
            double timeA{ 0.0 };
            double timeB{ 0.0 };

            std::unordered_map<std::string, int> clipIndex;
            std::unordered_map<std::string, size_t> boneNameToIndex;

            FbxAnimation anim;

            std::vector<DirectX::XMFLOAT4X4> paletteA;
            std::vector<DirectX::XMFLOAT4X4> paletteB;
            std::vector<DirectX::XMFLOAT4X4> blended;
            std::vector<DirectX::XMFLOAT4X4> globals;
        };

        void EnsureParams(Runtime& rt, AnimBlueprintComponent& bpComp)
        {
            for (const auto& p : rt.asset.params)
            {
                if (bpComp.params.find(p.name) != bpComp.params.end())
                    continue;
                AnimParamValue v;
                v.type = (p.type == AnimBPParamType::Bool) ? AnimParamType::Bool :
                         (p.type == AnimBPParamType::Int) ? AnimParamType::Int :
                         (p.type == AnimBPParamType::Trigger) ? AnimParamType::Trigger : AnimParamType::Float;
                v.b = p.b;
                v.i = p.i;
                v.f = p.f;
                v.trigger = p.trigger;
                bpComp.params[p.name] = v;
            }
        }

        void EnsureClipMap(Runtime& rt, const FbxModel& model)
        {
            if (!rt.clipIndex.empty())
                return;
            const auto& names = model.GetAnimationNames();
            for (int i = 0; i < (int)names.size(); ++i)
                rt.clipIndex[names[(size_t)i]] = i;
        }

        static bool CompareFloat(AnimBPCmpOp op, float a, float b)
        {
            switch (op)
            {
            case AnimBPCmpOp::EQ: return a == b;
            case AnimBPCmpOp::NEQ: return a != b;
            case AnimBPCmpOp::GT: return a > b;
            case AnimBPCmpOp::LT: return a < b;
            case AnimBPCmpOp::GTE: return a >= b;
            case AnimBPCmpOp::LTE: return a <= b;
            default: return false;
            }
        }

        bool CheckTransition(const Runtime& rt, const AnimBlueprintComponent& bpComp, const AnimBPTransition& tr, bool& usedTrigger) const
        {
            for (const auto& c : tr.conds)
            {
                auto it = bpComp.params.find(c.param);
                if (it == bpComp.params.end())
                    return false;

                const AnimParamValue& v = it->second;

                if (c.type == AnimBPParamType::Trigger)
                {
                    if (c.op == AnimBPCmpOp::IsSet && v.trigger)
                    {
                        usedTrigger = true;
                        continue;
                    }
                    return false;
                }

                if (c.type == AnimBPParamType::Bool)
                {
                    bool right = c.b;
                    bool left = v.b;
                    if (c.op == AnimBPCmpOp::EQ && left == right) continue;
                    if (c.op == AnimBPCmpOp::NEQ && left != right) continue;
                    return false;
                }

                if (c.type == AnimBPParamType::Int)
                {
                    if (!CompareFloat(c.op, (float)v.i, (float)c.i))
                        return false;
                    continue;
                }

                if (!CompareFloat(c.op, v.f, c.f))
                    return false;
            }
            return true;
        }

        void StepFSM(Runtime& rt, AnimBlueprintComponent& bpComp, double dtSec)
        {
            if (!bpComp.playing)
                return;

            const AnimBPState& cur = rt.asset.states[(size_t)rt.currentState];

            // 시간 진행
            const double speed = (bpComp.speed <= 0.0f) ? 0.0 : (double)bpComp.speed;
            rt.timeA += dtSec * speed * (double)cur.playRate;

            if (rt.inTransition)
            {
                rt.transitionTime += (float)dtSec;
                if (rt.transitionDuration > 0.0f && rt.transitionTime >= rt.transitionDuration)
                {
                    rt.inTransition = false;
                    rt.currentState = rt.nextState;
                    rt.nextState = -1;
                    rt.transitionTime = 0.0f;
                    rt.transitionDuration = 0.0f;
                    rt.timeA = rt.timeB;
                }
                else if (rt.nextState >= 0)
                {
                    const AnimBPState& next = rt.asset.states[(size_t)rt.nextState];
                    rt.timeB += dtSec * speed * (double)next.playRate;
                }

                if (!rt.canInterrupt)
                    return;
            }

            // 트랜지션 검사
            bool usedTrigger = false;
            for (const auto& tr : rt.asset.transitions)
            {
                if (tr.fromOut != cur.pinOut)
                    continue;

                int targetState = FindStateByInPin(rt, tr.toIn);
                if (targetState < 0)
                    continue;

                if (!CheckTransition(rt, bpComp, tr, usedTrigger))
                    continue;

                rt.inTransition = true;
                rt.canInterrupt = tr.canInterrupt;
                rt.transitionTime = 0.0f;
                rt.transitionDuration = tr.blendTime;
                rt.nextState = targetState;
                rt.timeB = 0.0;
                break;
            }

            if (usedTrigger)
            {
                for (auto& kv : bpComp.params)
                    if (kv.second.type == AnimParamType::Trigger)
                        kv.second.trigger = false;
            }
        }

        int FindStateByInPin(const Runtime& rt, std::uint64_t pinIn) const
        {
            for (size_t i = 0; i < rt.asset.states.size(); ++i)
                if (rt.asset.states[i].pinIn == pinIn)
                    return (int)i;
            return -1;
        }

        bool BuildPalette(Runtime& rt, const AnimBlueprintComponent& bpComp)
        {
            if (rt.currentState < 0 || rt.currentState >= (int)rt.asset.states.size())
                return false;

            const AnimBPState& cur = rt.asset.states[(size_t)rt.currentState];

            // === 1) 상태 간 트랜지션 블렌드 (기존 FSM 블렌딩) ===
            if (rt.inTransition && rt.nextState >= 0)
            {
                auto itA = rt.clipIndex.find(cur.clip);
                if (itA == rt.clipIndex.end())
                    return false;

                const AnimBPState& next = rt.asset.states[(size_t)rt.nextState];
                auto itB = rt.clipIndex.find(next.clip);
                if (itB == rt.clipIndex.end())
                {
                    rt.anim.BuildPaletteAt(itA->second, rt.timeA, rt.paletteA);
                    rt.blended = rt.paletteA;
                    TransposePalette(rt.blended);
                    return true;
                }

                rt.anim.BuildPaletteAt(itA->second, rt.timeA, rt.paletteA);
                rt.anim.BuildPaletteAt(itB->second, rt.timeB, rt.paletteB);

                const float alpha = (rt.transitionDuration > 0.0f)
                    ? std::min(1.0f, rt.transitionTime / rt.transitionDuration)
                    : 1.0f;

                const size_t count = std::min(rt.paletteA.size(), rt.paletteB.size());
                rt.blended.resize(count);

                for (size_t i = 0; i < count; ++i)
                {
                    DirectX::XMMATRIX a = DirectX::XMLoadFloat4x4(&rt.paletteA[i]);
                    DirectX::XMMATRIX b = DirectX::XMLoadFloat4x4(&rt.paletteB[i]);
                    DirectX::XMMATRIX m = a + (b - a) * alpha;
                    DirectX::XMStoreFloat4x4(&rt.blended[i], m);
                }

                TransposePalette(rt.blended);
                return true;
            }

            // === 2) 상태 내부 블렌드 그래프 평가 (간단 2-way Blend) ===
            // - AnimBlueprintEditor 의 BlendGraph 중 "Clip 2개 -> Blend 1개" 패턴만 인식
            // - 가중치는 AnimBlueprint 파라미터 "Speed"(0~1) 를 사용
            const AnimBPStateBlend* stateBlend = nullptr;
            for (const auto& sb : rt.asset.stateBlends)
            {
                if (sb.stateId == cur.id)
                {
                    stateBlend = &sb;
                    break;
                }
            }

            if (stateBlend)
            {
                auto itClipA = rt.clipIndex.find(stateBlend->clipA);
                auto itClipB = rt.clipIndex.find(stateBlend->clipB);
                if (itClipA != rt.clipIndex.end() && itClipB != rt.clipIndex.end())
                {
                    // Speed 파라미터(0~1)에서 weight 를 가져옴
                    float speed = 0.0f;
                    auto itParam = bpComp.params.find("Speed");
                    if (itParam != bpComp.params.end())
                        speed = itParam->second.f;
                    float w = std::clamp(speed, 0.0f, 1.0f);

                    // 현재 상태의 재생 시간 rt.timeA 기준으로 두 클립을 섞는다
                    rt.anim.BuildPaletteAt(itClipA->second, rt.timeA, rt.paletteA);
                    rt.anim.BuildPaletteAt(itClipB->second, rt.timeA, rt.paletteB);

                    const size_t count = std::min(rt.paletteA.size(), rt.paletteB.size());
                    rt.blended.resize(count);

                    for (size_t i = 0; i < count; ++i)
                    {
                        DirectX::XMMATRIX a = DirectX::XMLoadFloat4x4(&rt.paletteA[i]);
                        DirectX::XMMATRIX b = DirectX::XMLoadFloat4x4(&rt.paletteB[i]);
                        DirectX::XMMATRIX m = a + (b - a) * w;
                        DirectX::XMStoreFloat4x4(&rt.blended[i], m);
                    }

                    TransposePalette(rt.blended);
                    return true;
                }
            }

            // === 3) 블렌드 그래프가 없으면 단일 클립 재생 ===
            auto itSingle = rt.clipIndex.find(cur.clip);
            if (itSingle == rt.clipIndex.end())
                return false;

            rt.anim.BuildPaletteAt(itSingle->second, rt.timeA, rt.paletteA);
            rt.blended = rt.paletteA;
            TransposePalette(rt.blended);
            return true;
        }

        static void TransposePalette(std::vector<DirectX::XMFLOAT4X4>& pal)
        {
            for (auto& mat : pal)
            {
                DirectX::XMMATRIX m = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&mat));
                DirectX::XMStoreFloat4x4(&mat, m);
            }
        }

        void UpdateSockets(World& world, EntityId id, Runtime& rt)
        {
            auto* sockets = world.GetComponent<SocketComponent>(id);
            if (!sockets || sockets->sockets.empty())
                return;

            DirectX::XMMATRIX worldM = DirectX::XMMatrixIdentity();
            if (world.GetComponent<TransformComponent>(id))
                worldM = world.ComputeWorldMatrix(id);

            if (rt.currentState < 0) return;

            auto itClip = rt.clipIndex.find(rt.asset.states[(size_t)rt.currentState].clip);
            if (itClip == rt.clipIndex.end())
                return;

            rt.anim.EvaluateGlobalsAt(itClip->second, rt.timeA, rt.globals);
            if (rt.globals.empty())
                return;

            for (auto& s : sockets->sockets)
            {
                auto it = rt.boneNameToIndex.find(s.parentBone);
                if (it == rt.boneNameToIndex.end())
                    continue;

                const size_t boneIdx = it->second;
                if (boneIdx >= rt.globals.size())
                    continue;

                DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&s.scale);
                DirectX::XMVECTOR rotation = DirectX::XMLoadFloat3(&s.rotation);
                DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&s.position);
                DirectX::XMMATRIX local =
                    DirectX::XMMatrixScalingFromVector(scale) *
                    DirectX::XMMatrixRotationRollPitchYawFromVector(rotation) *
                    DirectX::XMMatrixTranslationFromVector(translation);

                // rt.globals is column-major (FBX evaluation); transpose to row-major.
                DirectX::XMMATRIX boneGRow = DirectX::XMMatrixTranspose(
                    DirectX::XMLoadFloat4x4(&rt.globals[boneIdx]));
                DirectX::XMMATRIX socketWorld = local * boneGRow * worldM;

                DirectX::XMStoreFloat4x4(&s.local, local);
                DirectX::XMStoreFloat4x4(&s.world, socketWorld);
            }
        }

    private:
        SkinnedMeshRegistry& m_registry;
        ResourceManager* m_resources = nullptr;
        std::unordered_map<EntityId, Runtime> m_runtime;
    };
}


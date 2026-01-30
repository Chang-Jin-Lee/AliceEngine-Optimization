#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

#include <DirectXMath.h>
#include <assimp/scene.h>

#include "Core/World.h"
#include "Core/Logger.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "3Dmodel/FbxModel.h"
#include "3Dmodel/FbxAnimation.h"
#include "Components/AdvancedAnimComponent.h"
#include "Components/AnimBlueprintComponent.h"
#include "Components/SocketComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkinnedAnimationComponent.h"
#include "Components/TransformComponent.h"

namespace Alice
{
    /// @brief 코드 기반 고급 애니메이션 시스템
    /// @details 블렌드, 상하체 분리, IK, Additive 애니메이션을 지원합니다.
    class AdvancedAnimSystem
    {
    public:
        explicit AdvancedAnimSystem(SkinnedMeshRegistry& registry)
            : m_registry(registry)
        {
        }

        void Update(World& world, double dtSec)
        {
            auto skinnedMap = world.GetComponents<SkinnedMeshComponent>();
            if (skinnedMap.empty())
                return;

            for (const auto& [entityId, skinned] : skinnedMap)
            {
                if (skinned.meshAssetPath.empty())
                    continue;

                if (const auto* tr = world.GetComponent<TransformComponent>(entityId); tr && !tr->enabled)
                    continue;

                auto* comp = world.GetComponent<AdvancedAnimComponent>(entityId);
                if (!comp || !comp->enabled)
                    continue;

                // AnimBlueprint가 있으면 이 시스템은 건너뜁니다
                if (world.GetComponent<AnimBlueprintComponent>(entityId))
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

                    BuildNodeCaches(mesh->sourceModel->GetScenePtr(),
                        mesh->sourceModel->GetNodeIndexOfName(),
                        rt.nodeNames,
                        rt.parentIndex);

                    rt.bindLocals.assign(rt.nodeNames.size(), FbxLocalSRT{});
                    BuildBindLocals(mesh->sourceModel->GetScenePtr(),
                        mesh->sourceModel->GetNodeIndexOfName(),
                        rt.bindLocals);

                    BuildClipMap(rt, *mesh->sourceModel);
                }

                if (rt.clipIndex.empty())
                    continue;

                // 시간 업데이트
                if (comp->playing && dtSec > 0.0)
                {
                    AdvanceLayerTime(comp->base, comp->globalSpeed, dtSec,
                        rt.timeBaseA, rt.timeBaseB);
                    AdvanceLayerTime(comp->upper, comp->globalSpeed, dtSec,
                        rt.timeUpperA, rt.timeUpperB);
                    AdvanceAdditiveTime(comp->additive, comp->globalSpeed, dtSec, rt.timeAdd);
                }

                // Locals 평가
                const int clipA = FindClip(rt, comp->base.clipA);
                const int clipB = FindClip(rt, comp->base.clipB);
                
                // clipA가 없으면 바인드 포즈를 사용합니다
                if (clipA < 0)
                {
                    // 바인드 포즈를 localsFinal에 직접 적용합니다
                    rt.localsFinal = rt.bindLocals;
                    BuildGlobalsFromLocals(rt.localsFinal, rt.parentIndex, rt.globals);
                    BuildPalette(*mesh->sourceModel, rt.globals, rt.palette);
                    TransposePalette(rt.palette);
                    
                    // SkinnedAnimationComponent에 결과 연결
                    auto* animComp = world.GetComponent<SkinnedAnimationComponent>(entityId);
                    if (!animComp)
                        animComp = &world.AddComponent<SkinnedAnimationComponent>(entityId);
                    animComp->palette = rt.palette;
                    
                    if (auto* skinnedWrite = world.GetComponent<SkinnedMeshComponent>(entityId))
                    {
                        skinnedWrite->boneMatrices = animComp->palette.data();
                        skinnedWrite->boneCount = static_cast<std::uint32_t>(animComp->palette.size());
                    }
                    
                    // 소켓 갱신
                    UpdateSockets(world, entityId, rt);
                    continue;
                }

                const double fadeDt = comp->playing ? dtSec : 0.0;
                const float baseBlend01 = UpdateCrossFade(comp->base, clipA, clipB, fadeDt,
                    rt, rt.timeBaseA, rt.timeBaseB, rt.baseFade);

                // 애니메이션 평가
                rt.anim.EvaluateLocalsAt(clipA, WrapTime(rt.timeBaseA, clipA, comp->base.loopA, rt),
                    rt.localsA, &rt.hasA);
                
                // 채널이 없는 본은 바인드 포즈로 채웁니다
                FillMissingFromBind(rt.bindLocals, rt.localsA, rt.hasA);
                
                // Translation 키가 없을 때 바인드 포즈 Translation을 사용합니다
                EnsureBindPoseTranslation(rt.bindLocals, rt.localsA, rt.hasA);

                #ifdef _DEBUG
                // 디버그: bindLocals와 localsA의 translation 길이 확인
                {
                    auto dbgLen = [](const DirectX::XMFLOAT3& v) { 
                        return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); 
                    };
                    const std::vector<std::string> testBones = { "Spine", "Chest", "Spine1", "Spine2", "Hips", "Root" };
                    for (const auto& boneName : testBones)
                    {
                        auto it = mesh->sourceModel->GetNodeIndexOfName().find(boneName);
                        if (it != mesh->sourceModel->GetNodeIndexOfName().end())
                        {
                            int idx = it->second;
                            if (idx >= 0 && (size_t)idx < rt.localsA.size() && (size_t)idx < rt.bindLocals.size())
                            {
                                float bindT = dbgLen(rt.bindLocals[idx].translation);
                                float localA = dbgLen(rt.localsA[idx].translation);
                                
                                if (bindT < 0.001f || localA < 0.001f)
                                {
                                    ALICE_LOG_ERRORF("[AdvancedAnim] Bone '%s' (idx=%d): bindT=%.6f localA=%.6f (CRITICAL: translation is zero!)",
                                        boneName.c_str(), idx, bindT, localA);
                                }
                                else
                                {
                                    ALICE_LOG_INFO("[AdvancedAnim] Bone '%s' (idx=%d): bindT=%.6f localA=%.6f (OK)",
                                        boneName.c_str(), idx, bindT, localA);
                                }
                            }
                        }
                    }
                }
                #endif

                if (comp->base.enabled && clipB >= 0)
                {
                    rt.anim.EvaluateLocalsAt(clipB, WrapTime(rt.timeBaseB, clipB, comp->base.loopB, rt),
                        rt.localsB, &rt.hasB);
                    FillMissingFromBind(rt.bindLocals, rt.localsB, rt.hasB);
                    EnsureBindPoseTranslation(rt.bindLocals, rt.localsB, rt.hasB);
                    BlendLocals(rt.localsA, rt.localsB, rt.baseBlended, baseBlend01);
                }
                else
                {
                    rt.baseBlended = rt.localsA;
                }

                rt.localsFinal = rt.baseBlended;

                // 상하체 분리 (Upper Layer)
                if (comp->upper.enabled && !comp->upper.clipA.empty())
                {
                    const int upperA = FindClip(rt, comp->upper.clipA);
                    const int upperB = FindClip(rt, comp->upper.clipB);
                    if (upperA >= 0)
                    {
                        const float upperBlend01 = UpdateCrossFade(comp->upper, upperA, upperB, fadeDt,
                            rt, rt.timeUpperA, rt.timeUpperB, rt.upperFade);

                        rt.anim.EvaluateLocalsAt(upperA, WrapTime(rt.timeUpperA, upperA, comp->upper.loopA, rt),
                            rt.localsUpperA, &rt.hasUpperA);
                        FillMissingFromBind(rt.bindLocals, rt.localsUpperA, rt.hasUpperA);
                        EnsureBindPoseTranslation(rt.bindLocals, rt.localsUpperA, rt.hasUpperA);

                        if (upperB >= 0)
                        {
                            rt.anim.EvaluateLocalsAt(upperB, WrapTime(rt.timeUpperB, upperB, comp->upper.loopB, rt),
                                rt.localsUpperB, &rt.hasUpperB);
                            FillMissingFromBind(rt.bindLocals, rt.localsUpperB, rt.hasUpperB);
                            EnsureBindPoseTranslation(rt.bindLocals, rt.localsUpperB, rt.hasUpperB);
                            BlendLocals(rt.localsUpperA, rt.localsUpperB, rt.upperBlended, upperBlend01);
                            BlendHasChannel(rt.hasUpperA, rt.hasUpperB, rt.hasUpperBlend);
                        }
                        else
                        {
                            rt.upperBlended = rt.localsUpperA;
                            rt.hasUpperBlend = rt.hasUpperA;
                        }

                        ApplyUpperLayer(comp->upper, comp->upperUseMask, comp->upperMaskKeywords,
                            rt.nodeNames, rt.upperBlended, rt.hasUpperBlend, rt.localsFinal);
                    }
                }

                // Additive 레이어
                if (comp->additive.enabled && !comp->additive.clip.empty())
                {
                    const int addClip = FindClip(rt, comp->additive.clip);
                    if (addClip >= 0)
                    {
                        const int refClip = FindClip(rt, comp->additive.refClip);
                        rt.anim.EvaluateLocalsAt(addClip, WrapTime(rt.timeAdd, addClip, comp->additive.loop, rt),
                            rt.localsAdd, &rt.hasAdd);
                        FillMissingFromBind(rt.bindLocals, rt.localsAdd, rt.hasAdd);
                        EnsureBindPoseTranslation(rt.bindLocals, rt.localsAdd, rt.hasAdd);
                        if (refClip >= 0)
                        {
                            rt.anim.EvaluateLocalsAt(refClip, comp->additive.refTime, rt.localsRef, &rt.hasRef);
                            FillMissingFromBind(rt.bindLocals, rt.localsRef, rt.hasRef);
                            EnsureBindPoseTranslation(rt.bindLocals, rt.localsRef, rt.hasRef);
                        }

                        ApplyAdditiveLayer(comp->additive, rt.localsAdd, rt.hasAdd, rt.localsRef, rt.localsFinal);
                    }
                }

                // IK (CCD)
                if (comp->ik.enabled && !comp->ik.tipBone.empty())
                {
                    ApplyIKCCD(world, entityId, rt, comp->ik, rt.localsFinal);
                }

                // Globals / Palette 생성
                #ifdef _DEBUG
                // 디버그: 특정 본의 translation 길이 확인
                {
                    auto dbgLen = [](const DirectX::XMFLOAT3& v) { 
                        return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); 
                    };
                    // Spine 또는 Chest 본 찾기
                    const std::vector<std::string> testBones = { "Spine", "Chest", "Spine1", "Spine2" };
                    for (const auto& boneName : testBones)
                    {
                        auto it = mesh->sourceModel->GetNodeIndexOfName().find(boneName);
                        if (it != mesh->sourceModel->GetNodeIndexOfName().end())
                        {
                            int idx = it->second;
                            if (idx >= 0 && (size_t)idx < rt.localsFinal.size() && (size_t)idx < rt.bindLocals.size())
                            {
                                float localT = dbgLen(rt.localsFinal[idx].translation);
                                float bindT = dbgLen(rt.bindLocals[idx].translation);
                                if (localT < 0.001f || bindT < 0.001f)
                                {
                                    ALICE_LOG_WARN("[AdvancedAnim] Bone '%s' (idx=%d): localT=%.6f bindT=%.6f (WARNING: translation too small!)",
                                        boneName.c_str(), idx, localT, bindT);
                                }
                            }
                        }
                    }
                }
                #endif

                BuildGlobalsFromLocals(rt.localsFinal, rt.parentIndex, rt.globals);

                #ifdef _DEBUG
                // 디버그: Advanced globals vs FbxAnimation ref globals 비교
                {
                    const int clipA = FindClip(rt, comp->base.clipA);
                    if (clipA >= 0)
                    {
                        std::vector<DirectX::XMFLOAT4X4> refGlobal;
                        rt.anim.EvaluateGlobalsAt(clipA, WrapTime(rt.timeBaseA, clipA, comp->base.loopA, rt), refGlobal);

                        auto Check = [&](const char* n)
                        {
                            auto it = mesh->sourceModel->GetNodeIndexOfName().find(n);
                            if (it == mesh->sourceModel->GetNodeIndexOfName().end()) return;
                            int idx = it->second;
                            if (idx < 0 || (size_t)idx >= rt.globals.size() || (size_t)idx >= refGlobal.size()) return;

                            auto L = [](const DirectX::XMFLOAT4X4& m)
                            {
                                return sqrtf(m._41*m._41 + m._42*m._42 + m._43*m._43);
                            };

                            float advLen = L(rt.globals[idx]);
                            float refLen = L(refGlobal[idx]);
                            
                            if (std::abs(advLen - refLen) > 0.1f)
                            {
                                ALICE_LOG_ERRORF("[AdvancedAnim] Globals mismatch '%s' (idx=%d): adv=%.4f ref=%.4f (DIFF=%.4f)",
                                    n, idx, advLen, refLen, std::abs(advLen - refLen));
                            }
                            else if (advLen < 0.001f && refLen > 0.001f)
                            {
                                ALICE_LOG_ERRORF("[AdvancedAnim] Globals CRITICAL '%s' (idx=%d): adv=%.4f ref=%.4f (Advanced is zero but ref is OK!)",
                                    n, idx, advLen, refLen);
                            }
                            else if (advLen < 0.001f && refLen < 0.001f)
                            {
                                // 둘 다 0이면 정상일 수 있습니다 (root 등)
                            }
                        };

                        Check("Hips");
                        Check("Spine");
                        Check("Spine1");
                        Check("Spine2");
                        Check("Chest");
                        Check("Head");
                        Check("Root");
                    }
                }
                #endif

                // 디버그용: Advanced 계산을 건너뛰고 ref 팔레트를 그대로 사용하는 옵션
                const bool USE_REF_PALETTE_FORCE = false;
                
                if (USE_REF_PALETTE_FORCE)
                {
                    const int clipA = FindClip(rt, comp->base.clipA);
                    if (clipA >= 0)
                    {
                        rt.anim.BuildPaletteAt(clipA, WrapTime(rt.timeBaseA, clipA, comp->base.loopA, rt), rt.palette);
                        TransposePalette(rt.palette);
                    }
                    else
                    {
                        BuildPalette(*mesh->sourceModel, rt.globals, rt.palette);
                        TransposePalette(rt.palette);
                    }
                }
                else
                {
                    BuildPalette(*mesh->sourceModel, rt.globals, rt.palette);
                    TransposePalette(rt.palette);
                }

                // SkinnedAnimationComponent에 결과 연결
                auto* animComp = world.GetComponent<SkinnedAnimationComponent>(entityId);
                if (!animComp)
                    animComp = &world.AddComponent<SkinnedAnimationComponent>(entityId);
                animComp->palette = rt.palette;

                if (auto* skinnedWrite = world.GetComponent<SkinnedMeshComponent>(entityId))
                {
                    skinnedWrite->boneMatrices = animComp->palette.data();
                    skinnedWrite->boneCount = static_cast<std::uint32_t>(animComp->palette.size());
                }

                // 소켓을 갱신합니다
                UpdateSockets(world, entityId, rt);
            }
        }

    private:
        struct Runtime
        {
            std::string meshKey;
            FbxAnimation anim;
            std::unordered_map<std::string, int> clipIndex;

            std::vector<std::string> nodeNames;
            std::vector<int> parentIndex;
            std::vector<FbxLocalSRT> bindLocals;

            double timeBaseA{ 0.0 };
            double timeBaseB{ 0.0 };
            double timeUpperA{ 0.0 };
            double timeUpperB{ 0.0 };
            double timeAdd{ 0.0 };

            std::vector<FbxLocalSRT> localsA;
            std::vector<FbxLocalSRT> localsB;
            std::vector<FbxLocalSRT> baseBlended;

            std::vector<FbxLocalSRT> localsUpperA;
            std::vector<FbxLocalSRT> localsUpperB;
            std::vector<FbxLocalSRT> upperBlended;

            std::vector<FbxLocalSRT> localsAdd;
            std::vector<FbxLocalSRT> localsRef;
            std::vector<FbxLocalSRT> localsFinal;

            std::vector<std::uint8_t> hasA;
            std::vector<std::uint8_t> hasB;
            std::vector<std::uint8_t> hasUpperA;
            std::vector<std::uint8_t> hasUpperB;
            std::vector<std::uint8_t> hasUpperBlend;
            std::vector<std::uint8_t> hasAdd;
            std::vector<std::uint8_t> hasRef;

            std::vector<DirectX::XMFLOAT4X4> globals;
            std::vector<DirectX::XMFLOAT4X4> palette;

            struct LayerFadeState
            {
                std::string clipA;
                std::string clipB;
                bool blending{ false };
                double elapsed{ 0.0 };
                float blend01{ 0.0f };
            };

            LayerFadeState baseFade;
            LayerFadeState upperFade;
        };

        static void BuildClipMap(Runtime& rt, const FbxModel& model)
        {
            rt.clipIndex.clear();
            const auto& names = model.GetAnimationNames();
            for (int i = 0; i < (int)names.size(); ++i)
                rt.clipIndex[names[(size_t)i]] = i;
        }

        static int FindClip(const Runtime& rt, const std::string& name)
        {
            if (name.empty())
                return -1;
            auto it = rt.clipIndex.find(name);
            return (it == rt.clipIndex.end()) ? -1 : it->second;
        }

        static void BuildNodeCaches(const aiScene* scene,
            const std::unordered_map<std::string, int>& nodeIndexOfName,
            std::vector<std::string>& outNames,
            std::vector<int>& outParents)
        {
            outNames.assign(nodeIndexOfName.size(), std::string{});
            outParents.assign(nodeIndexOfName.size(), -1);

            for (const auto& kv : nodeIndexOfName)
            {
                if (kv.second >= 0 && (size_t)kv.second < outNames.size())
                    outNames[(size_t)kv.second] = kv.first;
            }

            if (!scene || !scene->mRootNode)
                return;

            std::function<void(const aiNode*, int)> dfs = [&](const aiNode* node, int parentIdx)
            {
                auto it = nodeIndexOfName.find(node->mName.C_Str());
                int idx = (it != nodeIndexOfName.end()) ? it->second : -1;
                if (idx >= 0 && (size_t)idx < outParents.size())
                    outParents[(size_t)idx] = parentIdx;

                for (unsigned i = 0; i < node->mNumChildren; ++i)
                {
                    dfs(node->mChildren[i], idx);
                }
            };

            dfs(scene->mRootNode, -1);
        }

        /// @brief Assimp 행렬을 로컬 SRT로 분해합니다
        /// @details XMMatrixDecompose 대신 Assimp의 Decompose를 사용하여 FBX 노드 변환의 정확도를 보장합니다.
        static void DecomposeAiMatrixLocal(const aiMatrix4x4& m, FbxLocalSRT& out)
        {
            aiVector3D s, t;
            aiQuaternion r;
            m.Decompose(s, r, t);

            out.scale = { (float)s.x, (float)s.y, (float)s.z };
            out.translation = { (float)t.x, (float)t.y, (float)t.z };
            out.rotation = { (float)r.x, (float)r.y, (float)r.z, (float)r.w }; // (x,y,z,w)
        }

        static void BuildBindLocals(const aiScene* scene,
            const std::unordered_map<std::string, int>& nodeIndexOfName,
            std::vector<FbxLocalSRT>& out)
        {
            if (!scene || !scene->mRootNode || out.empty())
                return;

            std::function<void(const aiNode*)> dfs = [&](const aiNode* node)
            {
                if (!node)
                    return;

                auto it = nodeIndexOfName.find(node->mName.C_Str());
                if (it != nodeIndexOfName.end())
                {
                    const int idx = it->second;
                    if (idx >= 0 && (size_t)idx < out.size())
                    {
                        DecomposeAiMatrixLocal(node->mTransformation, out[(size_t)idx]);
                        // Scale 기본값을 (1, 1, 1)로 보장합니다
                        if (out[(size_t)idx].scale.x < 0.001f) out[(size_t)idx].scale.x = 1.0f;
                        if (out[(size_t)idx].scale.y < 0.001f) out[(size_t)idx].scale.y = 1.0f;
                        if (out[(size_t)idx].scale.z < 0.001f) out[(size_t)idx].scale.z = 1.0f;
                    }
                }

                for (unsigned i = 0; i < node->mNumChildren; ++i)
                    dfs(node->mChildren[i]);
            };

            dfs(scene->mRootNode);
        }

        static double WrapTime(double t, int clipIndex, bool loop, const Runtime& rt)
        {
            if (clipIndex < 0 || clipIndex >= (int)rt.anim.GetNames().size())
                return 0.0;

            const double len = rt.anim.GetClipDurationSec(clipIndex);
            if (len <= 0.0)
                return 0.0;

            if (!loop)
            {
                if (t < 0.0) return 0.0;
                if (t > len) return len;
                return t;
            }

            while (t < 0.0) t += len;
            while (t >= len) t -= len;
            return t;
        }

        static void AdvanceLayerTime(const AdvancedAnimLayer& layer,
            float globalSpeed, double dtSec, double& timeA, double& timeB)
        {
            if (!layer.enabled)
                return;

            const double spdA = std::max(0.0, (double)layer.speedA * (double)globalSpeed);
            const double spdB = std::max(0.0, (double)layer.speedB * (double)globalSpeed);
            timeA += dtSec * spdA;
            timeB += dtSec * spdB;
        }

        static void AdvanceAdditiveTime(const AdvancedAnimAdditive& add,
            float globalSpeed, double dtSec, double& timeAdd)
        {
            if (!add.enabled)
                return;
            const double spd = std::max(0.0, (double)add.speed * (double)globalSpeed);
            timeAdd += dtSec * spd;
        }

        static float Fade01(double elapsedSec, float durationSec, bool smoothStep)
        {
            float t = (durationSec > 0.0f) ? (float)(elapsedSec / durationSec) : 1.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            if (!smoothStep)
                return t;
            return t * t * (3.0f - 2.0f * t);
        }

        static float UpdateCrossFade(const AdvancedAnimLayer& layer, int clipA, int clipB,
            double dtSec, Runtime& rt, double& timeA, double& timeB, Runtime::LayerFadeState& state)
        {
            if (!layer.useCrossFade || clipA < 0 || clipB < 0)
            {
                state.blending = false;
                state.elapsed = 0.0;
                state.blend01 = layer.blend01;
                state.clipA = layer.clipA;
                state.clipB = layer.clipB;
                return layer.blend01;
            }

            const bool clipChanged = (state.clipA != layer.clipA) || (state.clipB != layer.clipB);
            if (clipChanged)
            {
                state.clipA = layer.clipA;
                state.clipB = layer.clipB;
                state.blending = false;
                state.elapsed = 0.0;
                state.blend01 = 0.0f;

                const double lenB = rt.anim.GetClipDurationSec(clipB);
                timeB = (lenB > 0.0) ? (double)layer.entryNorm * lenB : 0.0;
            }

            const double lenA = rt.anim.GetClipDurationSec(clipA);
            const double lenB = rt.anim.GetClipDurationSec(clipB);

            bool startBlend = false;
            if (!state.blending && state.blend01 < 1.0f)
            {
                if (!layer.useExitTime)
                {
                    startBlend = true;
                }
                else if (lenA > 0.0)
                {
                    const double tA = WrapTime(timeA, clipA, layer.loopA, rt);
                    const double norm = tA / lenA;
                    if (norm >= (double)layer.exitNorm)
                        startBlend = true;
                }
            }

            if (startBlend)
            {
                state.blending = true;
                state.elapsed = 0.0;
                if (lenB > 0.0)
                    timeB = (double)layer.entryNorm * lenB;
            }

            if (state.blending)
            {
                state.elapsed += dtSec;
                state.blend01 = Fade01(state.elapsed, layer.fadeDuration, layer.smoothStep);
                if (state.blend01 >= 1.0f - 1e-4f)
                {
                    state.blend01 = 1.0f;
                    state.blending = false;
                }
            }

            return state.blend01;
        }

        static void BlendLocals(const std::vector<FbxLocalSRT>& a,
            const std::vector<FbxLocalSRT>& b,
            std::vector<FbxLocalSRT>& out,
            float blend01)
        {
            out = a;
            if (a.size() != b.size())
                return;

            const float t = std::clamp(blend01, 0.0f, 1.0f);
            for (size_t i = 0; i < a.size(); ++i)
            {
                const DirectX::XMVECTOR Sa = DirectX::XMLoadFloat3(&a[i].scale);
                const DirectX::XMVECTOR Sb = DirectX::XMLoadFloat3(&b[i].scale);
                const DirectX::XMVECTOR Ta = DirectX::XMLoadFloat3(&a[i].translation);
                const DirectX::XMVECTOR Tb = DirectX::XMLoadFloat3(&b[i].translation);
                const DirectX::XMVECTOR Ra = DirectX::XMLoadFloat4(&a[i].rotation);
                const DirectX::XMVECTOR Rb = DirectX::XMLoadFloat4(&b[i].rotation);

                const DirectX::XMVECTOR S = DirectX::XMVectorLerp(Sa, Sb, t);
                const DirectX::XMVECTOR T = DirectX::XMVectorLerp(Ta, Tb, t);
                const DirectX::XMVECTOR R = DirectX::XMQuaternionSlerp(Ra, Rb, t);

                DirectX::XMStoreFloat3(&out[i].scale, S);
                DirectX::XMStoreFloat3(&out[i].translation, T);
                DirectX::XMStoreFloat4(&out[i].rotation, R);

                // 블렌드 후에도 Scale이 0이 되지 않도록 보장합니다
                if (out[i].scale.x < 0.001f) out[i].scale.x = 1.0f;
                if (out[i].scale.y < 0.001f) out[i].scale.y = 1.0f;
                if (out[i].scale.z < 0.001f) out[i].scale.z = 1.0f;
            }
        }

        static void FillMissingFromBind(const std::vector<FbxLocalSRT>& bindLocals,
            std::vector<FbxLocalSRT>& pose,
            const std::vector<std::uint8_t>& hasChannel)
        {
            if (bindLocals.size() != pose.size() || hasChannel.empty())
                return;

            for (size_t i = 0; i < pose.size(); ++i)
            {
                if (i < hasChannel.size() && hasChannel[i] == 0)
                {
                    pose[i] = bindLocals[i];
                    // bindLocals에서 복사한 후에도 Scale 기본값을 보장합니다
                    if (pose[i].scale.x < 0.001f) pose[i].scale.x = 1.0f;
                    if (pose[i].scale.y < 0.001f) pose[i].scale.y = 1.0f;
                    if (pose[i].scale.z < 0.001f) pose[i].scale.z = 1.0f;
                }
            }
        }

        /// @brief Translation 키가 없을 때 바인드 포즈 Translation을 사용합니다
        /// @details 애니메이션에 Translation 키가 없을 때 본이 뭉치는 현상을 방지합니다.
        static void EnsureBindPoseTranslation(const std::vector<FbxLocalSRT>& bindLocals,
            std::vector<FbxLocalSRT>& pose,
            const std::vector<std::uint8_t>& hasChannel)
        {
            if (bindLocals.size() != pose.size())
                return;

            for (size_t i = 0; i < pose.size() && i < bindLocals.size(); ++i)
            {
                // Translation 길이 계산
                const float localTlen = sqrtf(pose[i].translation.x * pose[i].translation.x +
                    pose[i].translation.y * pose[i].translation.y +
                    pose[i].translation.z * pose[i].translation.z);
                const float bindTlen = sqrtf(bindLocals[i].translation.x * bindLocals[i].translation.x +
                    bindLocals[i].translation.y * bindLocals[i].translation.y +
                    bindLocals[i].translation.z * bindLocals[i].translation.z);
                
                // 채널이 있는데 Translation이 0에 가깝고 바인드 포즈 Translation이 유의미하면 바인드 포즈 사용
                // (애니메이션에 Translation 키가 없을 때 바인드 포즈 Translation 사용)
                if (hasChannel.empty() || (i < hasChannel.size() && hasChannel[i] != 0))
                {
                    if (localTlen < 0.001f && bindTlen > 0.001f)
                    {
                        pose[i].translation = bindLocals[i].translation;
                    }
                }
            }
        }

        static void BlendHasChannel(const std::vector<std::uint8_t>& a,
            const std::vector<std::uint8_t>& b,
            std::vector<std::uint8_t>& out)
        {
            const size_t count = std::min(a.size(), b.size());
            out.resize(count);
            for (size_t i = 0; i < count; ++i)
                out[i] = (a[i] || b[i]) ? 1 : 0;
        }

        static std::vector<std::string> SplitKeywords(const std::string& s)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char c : s)
            {
                if (c == ',' || c == ';')
                {
                    if (!cur.empty())
                    {
                        out.push_back(cur);
                        cur.clear();
                    }
                }
                else if (!std::isspace(static_cast<unsigned char>(c)))
                {
                    cur.push_back((char)std::tolower((unsigned char)c));
                }
            }
            if (!cur.empty()) out.push_back(cur);
            return out;
        }

        static bool IsUpperBone(const std::string& name, const std::vector<std::string>& keywords)
        {
            if (keywords.empty())
                return true;

            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });

            for (const auto& k : keywords)
            {
                if (!k.empty() && lower.find(k) != std::string::npos)
                    return true;
            }
            return false;
        }

        static void ApplyUpperLayer(const AdvancedAnimLayer& upper, bool useMask,
            const std::string& maskKeywords,
            const std::vector<std::string>& nodeNames,
            const std::vector<FbxLocalSRT>& upperLocals,
            const std::vector<std::uint8_t>& upperHas,
            std::vector<FbxLocalSRT>& base)
        {
            if (upper.alpha <= 0.0f)
                return;

            const auto keywords = SplitKeywords(maskKeywords);
            const size_t count = std::min(base.size(), upperLocals.size());
            for (size_t i = 0; i < count; ++i)
            {
                if (i >= nodeNames.size())
                    continue;
                if (!upperHas.empty() && upperHas[i] == 0)
                    continue;
                if (useMask && !IsUpperBone(nodeNames[i], keywords))
                    continue;

                const float t = std::clamp(upper.alpha, 0.0f, 1.0f);
                const DirectX::XMVECTOR Sa = DirectX::XMLoadFloat3(&base[i].scale);
                const DirectX::XMVECTOR Sb = DirectX::XMLoadFloat3(&upperLocals[i].scale);
                const DirectX::XMVECTOR Ta = DirectX::XMLoadFloat3(&base[i].translation);
                const DirectX::XMVECTOR Tb = DirectX::XMLoadFloat3(&upperLocals[i].translation);
                const DirectX::XMVECTOR Ra = DirectX::XMLoadFloat4(&base[i].rotation);
                const DirectX::XMVECTOR Rb = DirectX::XMLoadFloat4(&upperLocals[i].rotation);

                const DirectX::XMVECTOR S = DirectX::XMVectorLerp(Sa, Sb, t);
                const DirectX::XMVECTOR T = DirectX::XMVectorLerp(Ta, Tb, t);
                const DirectX::XMVECTOR R = DirectX::XMQuaternionSlerp(Ra, Rb, t);

                DirectX::XMStoreFloat3(&base[i].scale, S);
                DirectX::XMStoreFloat3(&base[i].translation, T);
                DirectX::XMStoreFloat4(&base[i].rotation, R);

                // 상하체 레이어 적용 후에도 Scale이 0이 되지 않도록 보장합니다
                if (base[i].scale.x < 0.001f) base[i].scale.x = 1.0f;
                if (base[i].scale.y < 0.001f) base[i].scale.y = 1.0f;
                if (base[i].scale.z < 0.001f) base[i].scale.z = 1.0f;
            }
        }

        static void ApplyAdditiveLayer(const AdvancedAnimAdditive& add,
            const std::vector<FbxLocalSRT>& addLocals,
            const std::vector<std::uint8_t>& addHas,
            const std::vector<FbxLocalSRT>& refLocals,
            std::vector<FbxLocalSRT>& base)
        {
            const float w = std::clamp(add.weight, 0.0f, 1.0f);
            if (w <= 0.0f)
                return;

            const size_t count = std::min(base.size(), addLocals.size());
            for (size_t i = 0; i < count; ++i)
            {
                if (!addHas.empty() && addHas[i] == 0)
                    continue;

                const FbxLocalSRT& ref = (i < refLocals.size()) ? refLocals[i] : FbxLocalSRT{};

                const DirectX::XMVECTOR Sbase = DirectX::XMLoadFloat3(&base[i].scale);
                const DirectX::XMVECTOR Sadd = DirectX::XMLoadFloat3(&addLocals[i].scale);
                const DirectX::XMVECTOR Sref = DirectX::XMLoadFloat3(&ref.scale);

                const DirectX::XMVECTOR Tbase = DirectX::XMLoadFloat3(&base[i].translation);
                const DirectX::XMVECTOR Tadd = DirectX::XMLoadFloat3(&addLocals[i].translation);
                const DirectX::XMVECTOR Tref = DirectX::XMLoadFloat3(&ref.translation);

                const DirectX::XMVECTOR Rbase = DirectX::XMLoadFloat4(&base[i].rotation);
                const DirectX::XMVECTOR Radd = DirectX::XMLoadFloat4(&addLocals[i].rotation);
                const DirectX::XMVECTOR Rref = DirectX::XMLoadFloat4(&ref.rotation);

                const DirectX::XMVECTOR Tdelta = DirectX::XMVectorScale(DirectX::XMVectorSubtract(Tadd, Tref), w);
                const DirectX::XMVECTOR Tfinal = DirectX::XMVectorAdd(Tbase, Tdelta);

                const DirectX::XMVECTOR eps = DirectX::XMVectorSet(1e-6f, 1e-6f, 1e-6f, 1.0f);
                const DirectX::XMVECTOR SrefSafe = DirectX::XMVectorMax(Sref, eps);
                const DirectX::XMVECTOR Sratio = DirectX::XMVectorDivide(Sadd, SrefSafe);
                const DirectX::XMVECTOR Sfinal = DirectX::XMVectorMultiply(Sbase,
                    DirectX::XMVectorLerp(DirectX::XMVectorSet(1, 1, 1, 0), Sratio, w));

                const DirectX::XMVECTOR Rdelta = DirectX::XMQuaternionMultiply(Radd, DirectX::XMQuaternionInverse(Rref));
                const DirectX::XMVECTOR Rfinal = DirectX::XMQuaternionMultiply(Rbase,
                    DirectX::XMQuaternionSlerp(DirectX::XMQuaternionIdentity(), Rdelta, w));

                DirectX::XMStoreFloat3(&base[i].translation, Tfinal);
                DirectX::XMStoreFloat3(&base[i].scale, Sfinal);
                DirectX::XMStoreFloat4(&base[i].rotation, Rfinal);

                // Additive 레이어 적용 후에도 Scale이 0이 되지 않도록 보장합니다
                if (base[i].scale.x < 0.001f) base[i].scale.x = 1.0f;
                if (base[i].scale.y < 0.001f) base[i].scale.y = 1.0f;
                if (base[i].scale.z < 0.001f) base[i].scale.z = 1.0f;
            }
        }

        static void BuildGlobalsFromLocals(const std::vector<FbxLocalSRT>& locals,
            const std::vector<int>& parentIndex,
            std::vector<DirectX::XMFLOAT4X4>& outGlobals)
        {
            using namespace DirectX;

            const size_t count = locals.size();
            outGlobals.resize(count);

            // 부모 인덱스 < 자식 인덱스 특성을 이용해 순차 처리합니다
            for (size_t i = 0; i < count; ++i)
            {
                // Scale이 0이 되지 않도록 보장합니다 (행렬 조립 전에 확인)
                DirectX::XMFLOAT3 safeScale = locals[i].scale;
                if (safeScale.x < 0.001f) safeScale.x = 1.0f;
                if (safeScale.y < 0.001f) safeScale.y = 1.0f;
                if (safeScale.z < 0.001f) safeScale.z = 1.0f;

                DirectX::XMVECTOR S = XMLoadFloat3(&safeScale);
                DirectX::XMVECTOR R = XMQuaternionNormalize(XMLoadFloat4(&locals[i].rotation));
                DirectX::XMVECTOR T = XMLoadFloat3(&locals[i].translation);

                // FbxAnimation::EvaluateGlobals와 동일한 조립 순서로 통일합니다 (T*R*S)
                const DirectX::XMMATRIX L =
                    XMMatrixTranslationFromVector(T) *
                    XMMatrixRotationQuaternion(R) *
                    XMMatrixScalingFromVector(S);

                const int p = (i < parentIndex.size()) ? parentIndex[i] : -1;
                const DirectX::XMMATRIX P = (p >= 0) ? XMLoadFloat4x4(&outGlobals[p]) : XMMatrixIdentity();

                // parent * local 순서로 곱합니다
                XMStoreFloat4x4(&outGlobals[i], XMMatrixMultiply(P, L));
            }
        }

        static void BuildPalette(const FbxModel& model,
            const std::vector<DirectX::XMFLOAT4X4>& globals,
            std::vector<DirectX::XMFLOAT4X4>& outPalette)
        {
            const auto& boneNames = model.GetBoneNames();
            const auto& boneOffsets = model.GetBoneOffsets();
            const auto& nodeIndexOfName = model.GetNodeIndexOfName();
            const auto& globalInv = model.GetGlobalInverse();

            outPalette.assign(boneNames.size(), DirectX::XMFLOAT4X4(
                1,0,0,0,
                0,1,0,0,
                0,0,1,0,
                0,0,0,1));

            const DirectX::XMMATRIX Gi = DirectX::XMLoadFloat4x4(&globalInv);
            const bool isRigid = (model.GetCurrentAnimationType() == FbxModel::AnimationType::Rigid);

            for (size_t i = 0; i < boneNames.size(); ++i)
            {
                auto it = nodeIndexOfName.find(boneNames[i]);
                if (it == nodeIndexOfName.end())
                    continue;

                const int nodeIdx = it->second;
                if (nodeIdx < 0 || (size_t)nodeIdx >= globals.size())
                    continue;

                const DirectX::XMMATRIX G = DirectX::XMLoadFloat4x4(&globals[(size_t)nodeIdx]);
                DirectX::XMMATRIX M = Gi * G;
                if (!isRigid && i < boneOffsets.size())
                {
                    const DirectX::XMMATRIX Off = DirectX::XMLoadFloat4x4(&boneOffsets[i]);
                    M = M * Off;
                }
                DirectX::XMStoreFloat4x4(&outPalette[i], M);
            }
        }

        static void TransposePalette(std::vector<DirectX::XMFLOAT4X4>& pal)
        {
            for (auto& mat : pal)
            {
                DirectX::XMMATRIX m = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&mat));
                DirectX::XMStoreFloat4x4(&mat, m);
            }
        }

        static void ApplyIKCCD(World& world, EntityId id, const Runtime& rt,
            const AdvancedAnimIK& ik, std::vector<FbxLocalSRT>& locals)
        {
            if (rt.nodeNames.empty() || rt.parentIndex.empty())
                return;

            int tipIdx = -1;
            for (size_t i = 0; i < rt.nodeNames.size(); ++i)
            {
                if (rt.nodeNames[i] == ik.tipBone)
                {
                    tipIdx = (int)i;
                    break;
                }
            }
            if (tipIdx < 0)
                return;

            // World 공간에서 Model 공간으로 타겟 변환
            DirectX::XMFLOAT3 targetW = ik.targetWorld;
            DirectX::XMVECTOR target = DirectX::XMLoadFloat3(&targetW);
            if (auto* tr = world.GetComponent<TransformComponent>(id))
            {
                const DirectX::XMVECTOR S = DirectX::XMLoadFloat3(&tr->scale);
                const DirectX::XMVECTOR R = DirectX::XMLoadFloat3(&tr->rotation);
                const DirectX::XMVECTOR T = DirectX::XMLoadFloat3(&tr->position);
                const DirectX::XMMATRIX worldM =
                    DirectX::XMMatrixScalingFromVector(S) *
                    DirectX::XMMatrixRotationRollPitchYawFromVector(R) *
                    DirectX::XMMatrixTranslationFromVector(T);
                const DirectX::XMMATRIX invWorld = DirectX::XMMatrixInverse(nullptr, worldM);
                target = DirectX::XMVector3TransformCoord(target, invWorld);
            }

            const float weight = std::clamp(ik.weight, 0.0f, 1.0f);
            if (weight <= 0.0f)
                return;

            // CCD 알고리즘 반복
            const int iterations = std::max(1, ik.iterations);
            for (int iter = 0; iter < iterations; ++iter)
            {
                std::vector<DirectX::XMFLOAT4X4> globals;
                BuildGlobalsFromLocals(locals, rt.parentIndex, globals);

                int joint = tipIdx;
                for (int c = 0; c < ik.chainLength; ++c)
                {
                    joint = (joint >= 0 && joint < (int)rt.parentIndex.size())
                        ? rt.parentIndex[(size_t)joint]
                        : -1;
                    if (joint < 0) break;

                    const DirectX::XMMATRIX jointG = DirectX::XMLoadFloat4x4(&globals[(size_t)joint]);
                    DirectX::XMVECTOR jointS, jointR, jointT;
                    DirectX::XMMatrixDecompose(&jointS, &jointR, &jointT, jointG);

                    const DirectX::XMFLOAT4X4& endG = globals[(size_t)tipIdx];
                    const DirectX::XMVECTOR endPos = DirectX::XMVectorSet(endG._41, endG._42, endG._43, 1.0f);
                    DirectX::XMVECTOR v1 = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(endPos, jointT));
                    DirectX::XMVECTOR v2 = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(target, jointT));

                    DirectX::XMVECTOR v1Local = DirectX::XMVector3Rotate(v1, DirectX::XMQuaternionInverse(jointR));
                    DirectX::XMVECTOR v2Local = DirectX::XMVector3Rotate(v2, DirectX::XMQuaternionInverse(jointR));

                    DirectX::XMVECTOR axis = DirectX::XMVector3Cross(v1Local, v2Local);
                    float axisLen = DirectX::XMVectorGetX(DirectX::XMVector3Length(axis));
                    if (axisLen < 1e-6f)
                        continue;

                    axis = DirectX::XMVector3Normalize(axis);
                    float dot = DirectX::XMVectorGetX(DirectX::XMVector3Dot(v1Local, v2Local));
                    dot = std::clamp(dot, -1.0f, 1.0f);
                    float ang = std::acos(dot) * weight;

                    DirectX::XMVECTOR delta = DirectX::XMQuaternionRotationAxis(axis, ang);
                    DirectX::XMVECTOR cur = DirectX::XMLoadFloat4(&locals[(size_t)joint].rotation);
                    DirectX::XMVECTOR next = DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(delta, cur));
                    DirectX::XMStoreFloat4(&locals[(size_t)joint].rotation, next);
                }
            }
        }

        static void UpdateSockets(World& world, EntityId id, Runtime& rt)
        {
            auto* sockets = world.GetComponent<SocketComponent>(id);
            if (!sockets || sockets->sockets.empty())
                return;

            auto* tr = world.GetComponent<TransformComponent>(id);
            DirectX::XMMATRIX worldM = DirectX::XMMatrixIdentity();
            if (tr)
            {
                DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&tr->scale);
                DirectX::XMVECTOR rotation = DirectX::XMLoadFloat3(&tr->rotation);
                DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&tr->position);
                worldM = DirectX::XMMatrixScalingFromVector(scale)
                    * DirectX::XMMatrixRotationRollPitchYawFromVector(rotation)
                    * DirectX::XMMatrixTranslationFromVector(translation);
            }

            for (auto& s : sockets->sockets)
            {
                auto it = std::find(rt.nodeNames.begin(), rt.nodeNames.end(), s.parentBone);
                if (it == rt.nodeNames.end())
                    continue;
                const size_t boneIdx = (size_t)std::distance(rt.nodeNames.begin(), it);
                if (boneIdx >= rt.globals.size())
                    continue;

                DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&s.scale);
                DirectX::XMVECTOR rotation = DirectX::XMLoadFloat3(&s.rotation);
                DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&s.position);
                DirectX::XMMATRIX local =
                    DirectX::XMMatrixScalingFromVector(scale) *
                    DirectX::XMMatrixRotationRollPitchYawFromVector(rotation) *
                    DirectX::XMMatrixTranslationFromVector(translation);

                DirectX::XMMATRIX boneG = DirectX::XMLoadFloat4x4(&rt.globals[boneIdx]);
                DirectX::XMMATRIX socketWorld = local * boneG * worldM;

                DirectX::XMStoreFloat4x4(&s.local, local);
                DirectX::XMStoreFloat4x4(&s.world, socketWorld);
            }
        }

    private:
        SkinnedMeshRegistry& m_registry;
        std::unordered_map<EntityId, Runtime> m_runtime;
    };
}


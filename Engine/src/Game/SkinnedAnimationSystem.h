#pragma once

#include <unordered_map>
#include <string>

#include <DirectXMath.h>

#include "Core/World.h"
#include "Rendering/SkinnedMeshRegistry.h"
#include "3Dmodel/FbxModel.h"
#include "3Dmodel/FbxAnimation.h"
#include "Core/Logger.h"
#include "Components/AdvancedAnimationComponent.h"
#include "Components/SocketComponent.h"
#include "Components/TransformComponent.h"

namespace Alice
{
    /// SkinnedAnimationComponent(재생 상태) -> CPU 본 팔레트 계산 -> SkinnedMeshComponent.boneMatrices 연결
    /// - 엔티티 단위로 FbxAnimation 인스턴스를 유지하여, 같은 메시를 공유해도 개별 재생이 가능합니다.
    class SkinnedAnimationSystem
    {
    public:
        explicit SkinnedAnimationSystem(SkinnedMeshRegistry& registry)
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

                // AdvancedAnimationComponent가 있으면 이 시스템은 건너뜀
                if (world.GetComponent<AdvancedAnimationComponent>(entityId))
                    continue;

                const auto mesh = m_registry.Find(skinned.meshAssetPath);
                if (!mesh || !mesh->sourceModel)
                    continue;

                auto* animComp = world.GetComponent<SkinnedAnimationComponent>(entityId);
                if (!animComp)
                    animComp = &world.AddComponent<SkinnedAnimationComponent>(entityId);

                // 초기 팔레트 크기 보장
                const std::size_t boneCount = mesh->sourceModel->GetBoneNames().size();
                if (boneCount == 0)
                    continue;
                if (animComp->palette.size() != boneCount)
                {
                    animComp->palette.assign(boneCount, DirectX::XMFLOAT4X4(
                        1,0,0,0,
                        0,1,0,0,
                        0,0,1,0,
                        0,0,0,1));
                }

                // 엔티티별 런타임 애니메이터
                Runtime& rt = m_runtime[entityId];
                if (rt.meshKey != skinned.meshAssetPath)
                {
                    rt = Runtime{};
                    rt.meshKey = skinned.meshAssetPath;
                    rt.anim.InitMetadata(mesh->sourceModel->GetScenePtr());

                    // 공유 컨텍스트 바인딩(+프리컴퓨트)
                    rt.anim.SetSharedContext(
                        mesh->sourceModel->GetScenePtr(),
                        mesh->sourceModel->GetNodeIndexOfName(),
                        &mesh->sourceModel->GetBoneNames(),
                        &mesh->sourceModel->GetBoneOffsets(),
                        &mesh->sourceModel->GetGlobalInverse());

                    // 타입 설정 (Skinned/Rigid)
                    const auto t = mesh->sourceModel->GetCurrentAnimationType();
                    if (t == FbxModel::AnimationType::Rigid)  rt.anim.SetType(FbxAnimation::AnimType::Rigid);
                    else if (t == FbxModel::AnimationType::Skinned) rt.anim.SetType(FbxAnimation::AnimType::Skinned);
                    else rt.anim.SetType(FbxAnimation::AnimType::None);
                }

                const int clipCount = (int)rt.anim.GetNames().size();
                if (clipCount <= 0)
                    continue;

                // 상태 보정
                if (animComp->clipIndex < 0) animComp->clipIndex = 0;
                if (animComp->clipIndex >= clipCount) animComp->clipIndex = clipCount - 1;
                if (animComp->speed < 0.0f) animComp->speed = 0.0f;

                // 시간 진행(엔티티 단위)
                if (animComp->playing && dtSec > 0.0)
                    animComp->timeSec += dtSec * (double)animComp->speed;

                rt.anim.SetCurrentIndex(animComp->clipIndex);
                rt.anim.SetTimeSec(animComp->timeSec);

                // 팔레트 계산(전치 없음: ForwardRenderSystem에서 전치해서 업로드)
                rt.anim.BuildCurrentPaletteFloat4x4(animComp->palette);
				for (auto& mat : animComp->palette) {
					DirectX::XMMATRIX m = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&mat));
					DirectX::XMStoreFloat4x4(&mat, m);
				}

                // SocketComponent.sockets[].world 갱신 (SkinnedAnimation 사용 엔티티)
                if (auto* sockets = world.GetComponent<SocketComponent>(entityId))
                {
                    if (!sockets->sockets.empty())
                    {
                        DirectX::XMMATRIX worldRow = DirectX::XMMatrixIdentity();
                        if (world.GetComponent<TransformComponent>(entityId))
                            worldRow = world.ComputeWorldMatrix(entityId);

                        rt.anim.EvaluateGlobalsAtFull(animComp->clipIndex, animComp->timeSec, rt.globals);
                        if (!rt.globals.empty())
                        {
                            const auto& nodeIndexOfName = mesh->sourceModel->GetNodeIndexOfName();
                            for (auto& s : sockets->sockets)
                            {
                                auto it = nodeIndexOfName.find(s.parentBone);
                                if (it == nodeIndexOfName.end())
                                    continue;

                                const int nodeIdx = it->second;
                                if (nodeIdx < 0 || (size_t)nodeIdx >= rt.globals.size())
                                    continue;

                                DirectX::XMVECTOR scale = DirectX::XMLoadFloat3(&s.scale);
                                DirectX::XMVECTOR rotation = DirectX::XMLoadFloat3(&s.rotation);
                                DirectX::XMVECTOR translation = DirectX::XMLoadFloat3(&s.position);
                                DirectX::XMMATRIX localRow =
                                    DirectX::XMMatrixScalingFromVector(scale) *
                                    DirectX::XMMatrixRotationRollPitchYawFromVector(rotation) *
                                    DirectX::XMMatrixTranslationFromVector(translation);

                                // rt.globals is column-major (FBX evaluation); transpose to row-major.
                                DirectX::XMMATRIX boneGRow = DirectX::XMMatrixTranspose(
                                    DirectX::XMLoadFloat4x4(&rt.globals[(size_t)nodeIdx]));
                                DirectX::XMMATRIX socketWorld = localRow * boneGRow * worldRow;

                                DirectX::XMStoreFloat4x4(&s.local, localRow);
                                DirectX::XMStoreFloat4x4(&s.world, socketWorld);
                            }
                        }
                    }
                }

                // 렌더 시스템이 읽을 포인터 연결
                auto* skinnedWrite = world.GetComponent<SkinnedMeshComponent>(entityId);
                if (!skinnedWrite)
                    continue;
                skinnedWrite->boneMatrices = animComp->palette.data();
                skinnedWrite->boneCount = (std::uint32_t)animComp->palette.size();
            }
        }

    private:
        struct Runtime
        {
            std::string meshKey;
            FbxAnimation anim;
            std::vector<DirectX::XMFLOAT4X4> globals;
        };

        SkinnedMeshRegistry& m_registry;
        std::unordered_map<EntityId, Runtime> m_runtime;
    };
}



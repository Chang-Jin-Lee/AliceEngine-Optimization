#pragma once

#include <vector>
#include <functional>
#include <unordered_map>
#include <filesystem>

#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Rendering/Metrics/LegacyPathFlags.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Resources/ResourceManager.h"

namespace Alice
{
    /// World의 SkinnedMeshComponent를 순회하여
    /// ForwardRenderSystem에서 사용할 SkinnedDrawCommand 리스트를 생성합니다.
    /// - 애니메이션/스켈레탈 메시의 boneMatrices를 전달하면
    ///   스킨드 메시가 해당 본에 따라 렌더링되도록 변환됩니다.
    class SkinnedMeshSystem
    {
    public:
        explicit SkinnedMeshSystem(SkinnedMeshRegistry& registry)
            : m_registry(registry)
        {
        }

        /// World + Registry를 순회하여 렌더링 드로우 커맨드 리스트를 구성합니다.
        void BuildDrawList(const World& world,
            std::vector<SkinnedDrawCommand>& outCommands) const
        {
            outCommands.clear();

            const auto& skinnedMap = world.GetComponents<SkinnedMeshComponent>();
            if (skinnedMap.empty())
            {
                // 빈 상태(렌더링 드로우 커맨드가 하나도 없을 때) 조기 반환을 수행합니다.
                return;
            }

            {
                //ALICE_LOG_INFO("[SkinnedMeshSystem] BuildDrawList: skinnedComponents=%zu",
                //               skinnedMap.size());
            }

            for (const auto& [entityId, comp] : skinnedMap)
            {
                if (!comp.boneMatrices || comp.boneCount == 0)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: entity=%u no bones",
                    //               static_cast<unsigned>(entityId));
                    continue;
                }

                auto mesh = m_registry.Find(comp.meshAssetPath);
                if (!mesh)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: mesh not found for key=\"%s\"", comp.meshAssetPath.c_str());
                    continue;
                }

                const TransformComponent* t = world.GetComponent<TransformComponent>(entityId);
                if (!t)
                {
                    //ALICE_LOG_INFO("[SkinnedMeshSystem]  - skip: entity=%u no Transform", static_cast<unsigned>(entityId));
                    continue;
                }
                if (!t->enabled || !t->visible) continue;

                // 월드 행렬 계산 (c.txt 참조: 부모부터 루트까지 스택에 쌓고 역순으로 곱하기)
                using namespace DirectX;
                
                EntityId currentId = entityId;

                XMMATRIX worldM = XMMatrixIdentity();
                if (!LegacyPathFlags::Get().heapAllocWorldMatrix)
                {
                    while (currentId != InvalidEntityId)
                    {
                        const TransformComponent* tc = world.GetComponent<TransformComponent>(currentId);
                        if (!tc || !tc->enabled)
                            break;
                        XMVECTOR scale = XMLoadFloat3(&tc->scale);
                        XMVECTOR rotation = XMLoadFloat3(&tc->rotation);
                        XMVECTOR translation = XMLoadFloat3(&tc->position);
                        worldM = worldM * (XMMatrixScalingFromVector(scale) *
                            XMMatrixRotationRollPitchYawFromVector(rotation) *
                            XMMatrixTranslationFromVector(translation));
                        currentId = tc->parent;
                    }
                }
                else
                {
                    // OPTIMIZATION_REPORT P02: preserve the former heap-backed parent stack.
                    std::vector<XMMATRIX> matrixStack;
                    while (currentId != InvalidEntityId)
                    {
                        const TransformComponent* tc = world.GetComponent<TransformComponent>(currentId);
                        if (!tc || !tc->enabled)
                            break;
                        XMVECTOR scale = XMLoadFloat3(&tc->scale);
                        XMVECTOR rotation = XMLoadFloat3(&tc->rotation);
                        XMVECTOR translation = XMLoadFloat3(&tc->position);
                        matrixStack.push_back(XMMatrixScalingFromVector(scale) *
                            XMMatrixRotationRollPitchYawFromVector(rotation) *
                            XMMatrixTranslationFromVector(translation));
                        currentId = tc->parent;
                    }
                    for (const auto& m : matrixStack)
                        worldM = worldM * m;
                }

                SkinnedDrawCommand cmd = {};
                cmd.vertexBuffer = mesh->vertexBuffer.Get();
                cmd.indexBuffer = mesh->indexBuffer.Get();
                cmd.stride = mesh->stride;
                cmd.indexCount = mesh->indexCount;
                cmd.startIndex = mesh->startIndex;
                cmd.baseVertex = mesh->baseVertex;
                cmd.world = worldM;
                cmd.bones = comp.boneMatrices;
                cmd.boneCount = comp.boneCount;
                cmd.meshKey = comp.meshAssetPath;

                if (const MaterialComponent* mat = world.GetComponent<MaterialComponent>(entityId))
                {
                    cmd.color = mat->color;
                    cmd.alpha = mat->alpha;
                    cmd.roughness = mat->roughness;
                    cmd.metalness = mat->metalness;
                    cmd.ambientOcclusion = mat->ambientOcclusion;
                    cmd.envDiffuseStrength = mat->envDiffuseStrength;
                    cmd.envSpecularStrength = mat->envSpecularStrength;
                    cmd.emissiveColor = mat->emissiveColor;
                    cmd.emissiveIntensity = mat->emissiveIntensity;
                    cmd.emissiveBloom = mat->emissiveBloom;
                    cmd.normalStrength = mat->normalStrength;
                    cmd.shadingMode = mat->shadingMode;
                    cmd.transparent = mat->transparent;
                    cmd.outlineColor = mat->outlineColor;
                    cmd.outlineWidth = mat->outlineWidth;
                    cmd.albedoTexturePath = mat->albedoTexturePath;
                    cmd.emissiveTexturePath = mat->emissiveTexturePath;
                    if (cmd.emissiveTexturePath.empty() && !mat->assetPath.empty())
                    {
                        // 구(旧) 씬/프리팹에 emissiveTexturePath가 없는 경우를 위해 .mat에서 보강 로드
                        static std::unordered_map<std::string, std::string> s_matEmissivePathCache;
                        auto it = s_matEmissivePathCache.find(mat->assetPath);
                        if (it == s_matEmissivePathCache.end())
                        {
                            MaterialComponent loaded{};
                            std::string resolvedPath;
                            std::filesystem::path matPath = std::filesystem::path(mat->assetPath);
                            ResourceManager& resources = ResourceManager::Get();
                            const std::filesystem::path resolvedMatPath = resources.Resolve(matPath);
                            if (!resolvedMatPath.empty())
                            {
                                matPath = resolvedMatPath;
                            }
                            if (MaterialFile::Load(matPath, loaded, &resources))
                            {
                                resolvedPath = loaded.emissiveTexturePath;
                                if (resolvedPath.empty())
                                {
                                    ALICE_LOG_WARN("[SkinnedMeshSystem] Emissive fallback: material has no emissiveTexturePath. asset=\"%s\"",
                                                   mat->assetPath.c_str());
                                }
                            }
                            else
                            {
                                ALICE_LOG_WARN("[SkinnedMeshSystem] Emissive fallback: failed to load material asset=\"%s\" resolved=\"%s\"",
                                               mat->assetPath.c_str(),
                                               matPath.string().c_str());
                            }
                            it = s_matEmissivePathCache.emplace(mat->assetPath, std::move(resolvedPath)).first;
                        }

                        if (it != s_matEmissivePathCache.end() && !it->second.empty())
                        {
                            cmd.emissiveTexturePath = it->second;
                        }
                    }
                    cmd.toonPbrCuts = DirectX::XMFLOAT4(mat->toonPbrCut1, mat->toonPbrCut2, mat->toonPbrCut3, mat->toonPbrStrength);
                    cmd.toonPbrLevels = DirectX::XMFLOAT4(mat->toonPbrLevel1, mat->toonPbrLevel2, mat->toonPbrLevel3,
                                                          mat->toonPbrBlur ? 1.0f : 0.0f);
                    cmd.toonPbrAlphas = DirectX::XMFLOAT4(mat->toonPbrLevel1Alpha, mat->toonPbrLevel2Alpha, mat->toonPbrLevel3Alpha, mat->shadowStrength);
                    cmd.toonPbrRampIntensity = mat->toonPbrRampIntensity;
                    cmd.toonSelfShadowStrength = mat->toonSelfShadowStrength;

                    if (!mat->albedoTexturePath.empty())
                    {
                        //ALICE_LOG_INFO("[SkinnedMeshSystem] entity=%u mesh=\"%s\" albedoTex=\"%s\"",
                        //               static_cast<unsigned>(entityId),
                        //               comp.meshAssetPath.c_str(),
                        //               mat->albedoTexturePath.c_str());
                    }
                }
                else
                {
                    cmd.transparent = false;
                }

                // RenderDoc record: the legacy default forced an outline pass for every object.
                if (LegacyPathFlags::Get().outlineOnByDefault && cmd.outlineWidth <= 0.0f)
                    cmd.outlineWidth = 0.01f;

                outCommands.push_back(cmd);
            }

            //if (!outCommands.empty())
            //{
            //    // 실제로 렌더링될 스킨드 메시가 하나라도 있을 때만 로그를 출력합니다.
            //    ALICE_LOG_INFO("[SkinnedMeshSystem] BuildDrawList: commands=%zu",
            //                   outCommands.size());
            //}
        }

    private:
        SkinnedMeshRegistry& m_registry;
    };
}

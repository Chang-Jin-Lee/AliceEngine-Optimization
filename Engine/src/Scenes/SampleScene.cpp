#include "Scenes/SampleScene.h"

#include <DirectXMath.h>

#include "Core/ResourceManager.h"
#include <Game/FbxAsset.h>
#include <Core/Material.h>

namespace Alice
{
    using namespace DirectX;

    void SampleScene::OnEnter(World& world, ResourceManager& /*resources*/)
    {
		// 1) fbxasset 논리 경로
		const std::string instanceAssetLogicalPath = "Assets/Fbx/Cube.fbxasset";

		// 2) resolve -> abs
		std::filesystem::path absInstancePath = instanceAssetLogicalPath;
		if (!absInstancePath.is_absolute())
			absInstancePath = ResourceManager::Get().Resolve(absInstancePath);

		// 3) fbxasset 로드해서 mesh/material 경로 뽑기
		Alice::FbxInstanceAsset asset{};
		if (!Alice::LoadFbxInstanceAsset(absInstancePath, asset) || asset.meshAssetPath.empty())
		{
			// fallback: 기본 큐브(Transform만) + 기본 Material
			m_cubeEntity = world.CreateEntity();
			world.AddComponent<TransformComponent>(m_cubeEntity);
			world.AddComponent<MaterialComponent>(m_cubeEntity, XMFLOAT3(0.7f, 0.7f, 0.7f));
			return;
		}

		// 4) 엔티티 생성 + 트랜스폼
		m_cubeEntity = world.CreateEntity();

		auto& transform = world.AddComponent<TransformComponent>(m_cubeEntity);
		transform
			.SetPosition(0.0f, 0.0f, 0.0f)
			.SetScale(1.0f, 1.0f, 1.0f);

		// 5) **중요**: SkinnedMeshComponent에는 .fbxasset이 아니라 asset.meshAssetPath를 넣어야 함
		auto& skinned = world.AddComponent<SkinnedMeshComponent>(m_cubeEntity, asset.meshAssetPath);

		// 에디터 코드처럼 instanceAssetPath는 abs 경로 문자열로
		skinned.instanceAssetPath = absInstancePath.string();

		static XMFLOAT4X4 s_identityBone =
			XMFLOAT4X4(1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1);
		skinned.boneMatrices = &s_identityBone;
		skinned.boneCount = 1;

		// 6) 머티리얼도 에디터처럼 assetPath 세팅 + 로드까지
		if (!asset.materialAssetPaths.empty())
		{
			XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
			auto& mat = world.AddComponent<MaterialComponent>(m_cubeEntity, defaultColor);
			mat.assetPath = asset.materialAssetPaths.front();
			Alice::MaterialFile::Load(mat.assetPath, mat, &ResourceManager::Get());
		}
		else
		{
			world.AddComponent<MaterialComponent>(m_cubeEntity, XMFLOAT3(0.7f, 0.7f, 0.7f));
		}

		world.SetEntityName(m_cubeEntity, "Cube");
    }

    void SampleScene::OnExit(World& world, ResourceManager& /*resources*/)
    {
        if (m_cubeEntity != InvalidEntityId)
        {
            world.DestroyEntity(m_cubeEntity);
            m_cubeEntity = InvalidEntityId;
        }
    }

    void SampleScene::Update(World& world, ResourceManager& /*resources*/, float deltaTime)
    {
        if (m_cubeEntity == InvalidEntityId) return;

        auto* transform = world.GetComponent<TransformComponent>(m_cubeEntity);
        if (!transform) return;

        // 시간에 따라 Y축 회전
        transform->rotation.y += m_rotationSpeed * deltaTime;
    }

    // 이 씬을 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCENE(SampleScene);
}




#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include "Runtime/Rendering/Data/Material.h"
#include <DirectXMath.h>
#include <filesystem>

namespace Alice
{
	EntityId EditorCore::InstantiateFbxAssetToWorld(World& world,
		const std::filesystem::path& fbxAssetPath,
		std::string_view entityName)
	{
		Alice::FbxInstanceAsset asset{};
		std::filesystem::path abs = fbxAssetPath;

		// path가 논리 경로면 Resolve
		if (!abs.is_absolute())
			abs = ResourceManager::Get().Resolve(abs);

		if (!Alice::LoadFbxInstanceAsset(abs, asset) || asset.meshAssetPath.empty())
			return InvalidEntityId;

		// GPU 메시 없으면 원본 FBX 재임포트로 레지스트리 채움
		if (m_skinnedRegistry && m_renderDevice)
		{
			if (!m_skinnedRegistry->Find(asset.meshAssetPath))
			{
				FbxImportOptions opt{};
				FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);
				auto* device = m_renderDevice->GetDevice();

				std::filesystem::path src = asset.sourceFbx;
				if (!src.is_absolute())
					src = ResourceManager::Get().Resolve(src);

				importer.Import(device, src, opt);
			}
		}

		EntityId e = world.CreateEntity();

		auto& t = world.AddComponent<TransformComponent>(e);
		t.position = { 0, 0, 0 };
		t.rotation = { 0, 0, 0 };
		t.scale = { 1, 1, 1 };

		auto& skinned = world.AddComponent<SkinnedMeshComponent>(e, asset.meshAssetPath);
		skinned.instanceAssetPath = abs.string();

		static DirectX::XMFLOAT4X4 s_identityBone =
			DirectX::XMFLOAT4X4(1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1);
		skinned.boneMatrices = &s_identityBone;
		skinned.boneCount = 1;

		if (!asset.materialAssetPaths.empty())
		{
			DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
			auto& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
			mat.assetPath = asset.materialAssetPaths.front();
			MaterialFile::Load(mat.assetPath, mat, &ResourceManager::Get());
		}

		if (!entityName.empty())
			world.SetEntityName(e, std::string(entityName));

		g_SceneDirty = true;
		return e;
	}

	// ComponentEditCommandRTTR 구현
}


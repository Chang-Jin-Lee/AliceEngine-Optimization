#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/SceneFile.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Foundation/Logger.h"
#include <filesystem>

namespace Alice
{
	void EditorCore::EnsureSkinnedMeshesRegistered(World& world)
	{
		if (!m_skinnedRegistry || !m_renderDevice || world.GetComponents<SkinnedMeshComponent>().empty())
			return;

		for (const auto& [entityId, comp] : world.GetComponents<SkinnedMeshComponent>())
		{
			if (comp.meshAssetPath.empty() || m_skinnedRegistry->Find(comp.meshAssetPath))
				continue;

			// 경로 결정 (.fbxasset 우선, 없으면 관례 경로)
			std::filesystem::path fbxPath = comp.instanceAssetPath.empty()
				? std::filesystem::path("Assets/Fbx") / (comp.meshAssetPath + ".fbxasset")
				: std::filesystem::path(comp.instanceAssetPath);

			Alice::FbxInstanceAsset instance{};
			std::filesystem::path absPath = ResourceManager::Get().Resolve(fbxPath);

			// 로드 실패 검사
			if (!Alice::LoadFbxInstanceAsset(absPath, instance) || instance.sourceFbx.empty())
			{
				ALICE_LOG_WARN("[Editor] Failed loading fbxasset: %s", absPath.string().c_str());
				continue;
			}

			// 재임포트 및 등록
			FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);
			FbxImportResult res = importer.Import(m_renderDevice->GetDevice(), ResourceManager::Get().Resolve(instance.sourceFbx), {});

			ALICE_LOG_INFO("[Editor] Re-imported FBX: %s -> %s", instance.sourceFbx.c_str(), res.meshAssetPath.c_str());
		}
	}

	// 씬 저장

	void EditorCore::SaveScene(World& world)
	{
		std::filesystem::path savePath = g_CurrentScenePath.empty() ? "Assets/AutoSaved.scene" : g_CurrentScenePath;

		ALICE_LOG_INFO("[Editor] Saving Scene: %s", savePath.string().c_str());


		// 저장 실행
		std::filesystem::path absPath = Alice::ResourceManager::Get().Resolve(savePath);
		SceneFile::Save(world, absPath);

		// 상태 갱신
		g_CurrentScenePath = savePath;
		g_HasCurrentScenePath = true;
		g_SceneDirty = false;
	}

	// FBX 에셋을 월드에 인스턴스화

	void EditorCore::LoadScene(World& world)
	{
		// 이 함수는 더 이상 사용하지 않음. SceneManager::LoadSceneFileRequest을 사용해야 함.
		// 하지만 호환성을 위해 남겨둠 (내부적으로는 즉시 로드)
		ALICE_LOG_WARN("[Editor] LoadScene() is deprecated. Use SceneManager::LoadSceneFileRequest() instead.");

		const std::filesystem::path loadAbs = ResourceManager::Get().Resolve(g_NextScenePath);

		// 로드 실행 및 반환값 체크
		if (!SceneFile::Load(world, loadAbs))
		{
			// 로드 실패: 에러 로그 및 팝업 표시
			const std::string errorMsg = "씬 로드 실패: " + g_NextScenePath.string() + "\n\n파일을 읽거나 역직렬화하는 중 오류가 발생했습니다.\n일부 컴포넌트만 로드되었을 수 있습니다.";
			ALICE_LOG_ERRORF("[Editor] Scene load failed: %s", g_NextScenePath.string().c_str());

			g_SceneLoadErrorMsg = errorMsg;
			g_ShowSceneLoadError = true;

			// 후처리하지 않고 종료 (부분 로드 방지)
			return;
		}

		// 로드 성공: 후처리 및 상태 갱신
		EnsureSkinnedMeshesRegistered(world);
		g_CurrentScenePath = g_NextScenePath;
		g_HasCurrentScenePath = true;
		g_SceneDirty = false;
	}

}


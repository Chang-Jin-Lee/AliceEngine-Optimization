#include "Core/Scene.h"
#include "Core/ResourceManager.h"
#include "Core/SceneFile.h"
#include "Core/Logger.h"

namespace Alice
{
	namespace
	{
		using Registry = std::unordered_map<std::string, SceneCreateFunc>;

		Registry& GetRegistry()
		{
			static Registry s_registry;
			return s_registry;
		}
	}

	void SceneFactory::Register(const char* name, SceneCreateFunc func)
	{
		if (!name || !func) return;
		GetRegistry()[name] = func;
	}

	std::unique_ptr<IScene> SceneFactory::Create(const char* name)
	{
		if (!name) return nullptr;

		auto& registry = GetRegistry();
		auto  it = registry.find(name);
		if (it == registry.end()) return nullptr;

		return std::unique_ptr<IScene>(it->second());
	}

	SceneManager::SceneManager(World& world, ResourceManager& resources)
		: m_world(world)
		, m_resources(resources)
	{
	}

	// =========================
	// 즉시 전환 (엔진 안전 지점)
	// =========================
	bool SceneManager::SwitchToImmediate(const char* sceneName)
	{
		auto newScene = SceneFactory::Create(sceneName);
		if (!newScene) return false;

		if (m_currentScene)
			m_currentScene->OnExit(m_world, m_resources);

		m_currentScene = std::move(newScene);
		m_currentScene->OnEnter(m_world, m_resources);
		// 코드 씬으로 전환 시 파일 경로 초기화
		m_currentSceneFilePath.clear();
		return true;
	}

	// =========================
	// 지연 전환 요청 (스크립트에서 호출 안전)
	// =========================
	bool SceneManager::SwitchTo(const char* sceneName)
	{
		auto newScene = SceneFactory::Create(sceneName);
		if (!newScene) return false;

		m_pendingScene = std::move(newScene);
		m_pendingSceneFile.reset(); // 파일 로드 요청이 있던 걸 덮어씀
		return true;
	}

	bool SceneManager::LoadSceneFileRequest(const std::filesystem::path& logicalScenePath)
	{
		if (logicalScenePath.empty()) return false;

		// ".scene" 같은 닷파일/필터 문자열 방지
		if (!logicalScenePath.has_extension()) return false;
		if (logicalScenePath.extension() != ".scene") return false;

		m_pendingSceneFile = logicalScenePath;
		m_pendingScene.reset(); // 코드 씬 전환 요청이 있던 걸 덮어씀
		return true;
	}

	void SceneManager::Update(float deltaTime)
	{
		if (!m_currentScene) return;
		m_currentScene->Update(m_world, m_resources, deltaTime);
	}

	EntityId SceneManager::GetPrimaryRenderableEntity() const
	{
		if (!m_currentScene) return InvalidEntityId;
		return m_currentScene->GetPrimaryRenderableEntity();
	}

	bool SceneManager::HasPendingSceneChange() const
	{
		return (m_pendingScene != nullptr) || m_pendingSceneFile.has_value();
	}

	bool SceneManager::CommitPendingSceneChange(World& world)
	{
		// pending 데이터 추출
		std::unique_ptr<IScene> pendingScene = std::move(m_pendingScene);
		std::optional<std::filesystem::path> pendingFile = std::move(m_pendingSceneFile);
		m_pendingSceneFile.reset();

		// (A) 코드 기반 씬 전환 커밋
		if (pendingScene)
		{
			if (m_currentScene)
				m_currentScene->OnExit(m_world, m_resources);

			m_currentScene = std::move(pendingScene);
			m_currentScene->OnEnter(m_world, m_resources);
			
			// 코드 씬 이름 저장
			if (m_currentScene)
			{
				m_currentSceneName = m_currentScene->GetName() ? m_currentScene->GetName() : "";
			}
			// 코드 기반 씬 전환 시 파일 경로 초기화
			m_currentSceneFilePath.clear();
			return true;
		}

		// (B) .scene 파일 로드 커밋
		if (pendingFile.has_value())
		{
			const auto path = *pendingFile;

			if (m_currentScene)
				m_currentScene->OnExit(m_world, m_resources);

			// 파일 기반 로드면 "현재 코드 씬" 개념이 없어질 수 있으니 비워둠
			m_currentScene.reset();

			const bool ok = SceneFile::LoadAuto(world, m_resources, path);

			if (ok)
			{
				// 로드 성공 시 현재 씬 파일 경로 저장
				m_currentSceneFilePath = path;
			}
			else
			{
				ALICE_LOG_ERRORF("[SceneManager] SceneFile::LoadAuto failed: %s", path.generic_string().c_str());
				// 로드 실패 시 경로는 유지하지 않음
			}
			return ok;
		}

		return false;
	}
}

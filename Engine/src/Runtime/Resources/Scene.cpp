#include "Runtime/Resources/Scene.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/SceneFile.h"
#include "Runtime/Audio/SoundManager.h"
#include "Runtime/Foundation/Logger.h"

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
	// ì¦‰ì‹œ ?„í™˜ (?”ì§„ ?ˆì „ ì§€??
	// =========================
	bool SceneManager::SwitchToImmediate(const char* sceneName)
	{
		auto newScene = SceneFactory::Create(sceneName);
		if (!newScene) return false;

		if (m_currentScene)
			m_currentScene->OnExit(m_world, m_resources);

		m_currentScene = std::move(newScene);
		m_currentScene->OnEnter(m_world, m_resources);
		// ì½”ë“œ ?¬ìœ¼ë¡??„í™˜ ???Œì¼ ê²½ë¡œ ì´ˆê¸°??
		m_currentSceneFilePath.clear();
		return true;
	}

	// =========================
	// ì§€???„í™˜ ?”ì²­ (?¤í¬ë¦½íŠ¸?ì„œ ?¸ì¶œ ?ˆì „)
	// =========================
	bool SceneManager::SwitchTo(const char* sceneName)
	{
		auto newScene = SceneFactory::Create(sceneName);
		if (!newScene) return false;

		m_pendingScene = std::move(newScene);
		m_pendingSceneFile.reset(); // ?Œì¼ ë¡œë“œ ?”ì²­???ˆë˜ ê±???–´?€
		return true;
	}

	bool SceneManager::LoadSceneFileRequest(const std::filesystem::path& logicalScenePath)
	{
		if (logicalScenePath.empty()) return false;

		// ".scene" ê°™ì? ?·íŒŒ???„í„° ë¬¸ì??ë°©ì?
		if (!logicalScenePath.has_extension()) return false;
		if (logicalScenePath.extension() != ".scene") return false;

		m_pendingSceneFile = logicalScenePath;
		m_pendingScene.reset(); // ì½”ë“œ ???„í™˜ ?”ì²­???ˆë˜ ê±???–´?€
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
		// Stop any playing audio before tearing down/loading scenes.
		Sound::StopBGM();
		Sound::StopAllSFX();

		// pending ?°ì´??ì¶”ì¶œ
		std::unique_ptr<IScene> pendingScene = std::move(m_pendingScene);
		std::optional<std::filesystem::path> pendingFile = std::move(m_pendingSceneFile);
		m_pendingSceneFile.reset();

		// (A) ì½”ë“œ ê¸°ë°˜ ???„í™˜ ì»¤ë°‹
		if (pendingScene)
		{
			if (m_currentScene)
				m_currentScene->OnExit(m_world, m_resources);

			m_currentScene = std::move(pendingScene);
			m_currentScene->OnEnter(m_world, m_resources);
			
			// ì½”ë“œ ???´ë¦„ ?€??
			if (m_currentScene)
			{
				m_currentSceneName = m_currentScene->GetName() ? m_currentScene->GetName() : "";
			}
			// ì½”ë“œ ê¸°ë°˜ ???„í™˜ ???Œì¼ ê²½ë¡œ ì´ˆê¸°??
			m_currentSceneFilePath.clear();
			return true;
		}

		// (B) .scene ?Œì¼ ë¡œë“œ ì»¤ë°‹
		if (pendingFile.has_value())
		{
			const auto path = *pendingFile;

			if (m_currentScene)
				m_currentScene->OnExit(m_world, m_resources);

			// ?Œì¼ ê¸°ë°˜ ë¡œë“œë©?"?„ì¬ ì½”ë“œ ?? ê°œë…???†ì–´ì§????ˆìœ¼??ë¹„ì›Œ??
			m_currentScene.reset();

			const bool ok = SceneFile::LoadAuto(world, m_resources, path);

			if (ok)
			{
				// ë¡œë“œ ?±ê³µ ???„ì¬ ???Œì¼ ê²½ë¡œ ?€??
				m_currentSceneFilePath = path;
			}
			else
			{
				ALICE_LOG_ERRORF("[SceneManager] SceneFile::LoadAuto failed: %s", path.generic_string().c_str());
				// ë¡œë“œ ?¤íŒ¨ ??ê²½ë¡œ??? ì??˜ì? ?ŠìŒ
			}
			return ok;
		}

		return false;
	}
}


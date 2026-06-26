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
	// 利됱떆 ?꾪솚 (?붿쭊 ?덉쟾 吏??
	// =========================
	bool SceneManager::SwitchToImmediate(const char* sceneName)
	{
		auto newScene = SceneFactory::Create(sceneName);
		if (!newScene) return false;

		if (m_currentScene)
			m_currentScene->OnExit(m_world, m_resources);

		m_currentScene = std::move(newScene);
		m_currentScene->OnEnter(m_world, m_resources);
		// 肄붾뱶 ?ъ쑝濡??꾪솚 ???뚯씪 寃쎈줈 珥덇린??
		m_currentSceneFilePath.clear();
		return true;
	}

	// =========================
	// 吏???꾪솚 ?붿껌 (?ㅽ겕由쏀듃?먯꽌 ?몄텧 ?덉쟾)
	// =========================
	bool SceneManager::SwitchTo(const char* sceneName)
	{
		auto newScene = SceneFactory::Create(sceneName);
		if (!newScene) return false;

		m_pendingScene = std::move(newScene);
		m_pendingSceneFile.reset(); // ?뚯씪 濡쒕뱶 ?붿껌???덈뜕 嫄???뼱?
		return true;
	}

	bool SceneManager::LoadSceneFileRequest(const std::filesystem::path& logicalScenePath)
	{
		if (logicalScenePath.empty()) return false;

		// ".scene" 媛숈? ?룻뙆???꾪꽣 臾몄옄??諛⑹?
		if (!logicalScenePath.has_extension()) return false;
		if (logicalScenePath.extension() != ".scene") return false;

		m_pendingSceneFile = logicalScenePath;
		m_pendingScene.reset(); // 肄붾뱶 ???꾪솚 ?붿껌???덈뜕 嫄???뼱?
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

		// pending ?곗씠??異붿텧
		std::unique_ptr<IScene> pendingScene = std::move(m_pendingScene);
		std::optional<std::filesystem::path> pendingFile = std::move(m_pendingSceneFile);
		m_pendingSceneFile.reset();

		// (A) 肄붾뱶 湲곕컲 ???꾪솚 而ㅻ컠
		if (pendingScene)
		{
			if (m_currentScene)
				m_currentScene->OnExit(m_world, m_resources);

			m_currentScene = std::move(pendingScene);
			m_currentScene->OnEnter(m_world, m_resources);
			
			// 肄붾뱶 ???대쫫 ???
			if (m_currentScene)
			{
				m_currentSceneName = m_currentScene->GetName() ? m_currentScene->GetName() : "";
			}
			// 肄붾뱶 湲곕컲 ???꾪솚 ???뚯씪 寃쎈줈 珥덇린??
			m_currentSceneFilePath.clear();
			return true;
		}

		// (B) .scene ?뚯씪 濡쒕뱶 而ㅻ컠
		if (pendingFile.has_value())
		{
			const auto path = *pendingFile;

			if (m_currentScene)
				m_currentScene->OnExit(m_world, m_resources);

			// ?뚯씪 湲곕컲 濡쒕뱶硫?"?꾩옱 肄붾뱶 ?? 媛쒕뀗???놁뼱吏????덉쑝??鍮꾩썙??
			m_currentScene.reset();

			const bool ok = SceneFile::LoadAuto(world, m_resources, path);

			if (ok)
			{
				// 濡쒕뱶 ?깃났 ???꾩옱 ???뚯씪 寃쎈줈 ???
				m_currentSceneFilePath = path;
			}
			else
			{
				ALICE_LOG_ERRORF("[SceneManager] SceneFile::LoadAuto failed: %s", path.generic_string().c_str());
				// 濡쒕뱶 ?ㅽ뙣 ??寃쎈줈???좎??섏? ?딆쓬
			}
			return ok;
		}

		return false;
	}
}


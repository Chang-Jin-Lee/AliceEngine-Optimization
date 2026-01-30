#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <optional>
#include <filesystem>

#include "Core/Entity.h"
#include "Core/World.h"

// 전방 선언
class UIWorldManager;

namespace Alice
{
	class ResourceManager;

	/// 모든 씬이 공통으로 구현해야 하는 최소 인터페이스입니다.
	class IScene
	{
	public:
		virtual ~IScene() = default;

		virtual const char* GetName() const = 0;
		virtual void OnEnter(World& world, ResourceManager& resources) { (void)world; (void)resources; }
		virtual void OnExit(World& world, ResourceManager& resources) { (void)world; (void)resources; }
		virtual void Update(World& world, ResourceManager& resources, float deltaTime) = 0;

		virtual EntityId GetPrimaryRenderableEntity() const { return InvalidEntityId; }
	};

	using SceneCreateFunc = IScene * (*)();

	class SceneFactory
	{
	public:
		static void Register(const char* name, SceneCreateFunc func);
		static std::unique_ptr<IScene> Create(const char* name);
	};

	template <typename TScene>
	class SceneRegistrar
	{
	public:
		explicit SceneRegistrar(const char* name)
		{
			SceneFactory::Register(name, []() -> IScene*
				{
					return new TScene();
				});
		}
	};

	/// 현재 활성 씬 한 개를 관리하는 간단한 매니저입니다.
	/// 핵심 규칙:
	/// - 게임 루프 도중(특히 Script Tick 안)에는 "즉시 전환"을 하지 않는다.
	/// - SwitchTo/LoadSceneFileRequest 로 "요청"만 걸어두고,
	/// - Engine::Update 안전 지점에서 CommitPendingSceneChange 로 커밋한다.
	class SceneManager
	{
	public:
		SceneManager(World& world, ResourceManager& resources);

		/// (즉시 전환) 엔진 초기화/안전 지점에서만 쓰는 함수
		bool SwitchToImmediate(const char* sceneName);

		/// (지연 전환 요청) 스크립트/게임플레이에서 호출해도 안전
		bool SwitchTo(const char* sceneName);

		/// (지연 전환 요청) .scene 파일 로드도 지연 커밋으로 처리
		bool LoadSceneFileRequest(const std::filesystem::path& logicalScenePath);

		/// 현재 씬 업데이트
		void Update(float deltaTime);

		/// 현재 씬의 대표 렌더링 엔티티 ID
		EntityId GetPrimaryRenderableEntity() const;

		/// 엔진이 확인용으로 쓰는 API
		bool HasPendingSceneChange() const;

		/// 엔진이 "프레임 경계"에서만 호출해야 하는 커밋 API
		/// 성공 시 true
		bool CommitPendingSceneChange(World& world, UIWorldManager* uiWorldManager = nullptr);

		/// 현재 씬 이름을 반환합니다 (없으면 nullptr)
		const char* GetCurrentSceneName() const { return m_currentSceneName.c_str(); };

		/// 현재 로드된 씬 파일 경로를 반환합니다 (파일 기반 씬인 경우)
		/// 파일 기반 씬이 아니면 빈 경로를 반환합니다.
		const std::filesystem::path& GetCurrentSceneFilePath() const { return m_currentSceneFilePath; }

	private:
		World& m_world;
		ResourceManager& m_resources;

		std::unique_ptr<IScene> m_currentScene;

		// pending(지연) 전환 요청
		std::unique_ptr<IScene> m_pendingScene;
		std::optional<std::filesystem::path> m_pendingSceneFile;

		// 현재 로드된 씬 파일 경로 (에디터에서 저장 경로 추적용)
		std::filesystem::path m_currentSceneFilePath;
		
		// 현재 로드된 Scene 이름 (파일에서 읽어온 이름 또는 코드 씬 이름)
		std::string m_currentSceneName;
	};

#define REGISTER_SCENE(SceneType) \
        static Alice::SceneRegistrar<SceneType> s_scene_registrar_##SceneType(#SceneType);
}

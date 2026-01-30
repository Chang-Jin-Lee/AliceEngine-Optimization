#pragma once

#include <filesystem>
#include <string>

// 전방 선언
class UIWorldManager;

namespace Alice
{
    class World;
    class ResourceManager;

    /// 씬(.scene) 파일 저장/로드 유틸리티입니다.
    /// - JSON 기반 저장/로드입니다.
    // 에디터에서 씬을 만들고, 저장하는 기능에 해당하는 코드임
    namespace SceneFile
    {
        /// 현재 World 의 상태를 JSON(.scene)으로 저장합니다.
        bool Save(const World& world, const std::filesystem::path& path);
        
        /// World와 UI를 함께 저장합니다.
        /// (현재는 UI를 .scene에 통합 저장합니다. uiWorldManager는 사용하지 않습니다.)
        bool Save(const World& world, const std::filesystem::path& path, UIWorldManager* uiWorldManager);

        /// .scene(JSON)을 읽어서 World 를 재구성합니다.
        /// 기존 엔티티들은 모두 제거됩니다.
        bool Load(World& world, const std::filesystem::path& path);
        
        /// World와 UI를 함께 로드합니다.
        /// (현재는 UI를 .scene에서 함께 로드합니다. uiWorldManager는 사용하지 않습니다.)
        bool Load(World& world, const std::filesystem::path& path, UIWorldManager* uiWorldManager);

        /// World 상태를 JSON 문자열로 직렬화합니다. (Play 스냅샷용)
        bool SaveToJsonString(const World& world, std::string& out);

        /// JSON 문자열에서 World 를 복원합니다. (Stop 시 편집본 복원용)
        /// 기존 엔티티는 Clear 후 로드됩니다.
        bool LoadFromJsonString(World& world, const std::string& json);

        /// 에디터/최종빌드 모두에서 동작하는 자동 로더입니다.
        /// - editorMode: 실제 파일(Assets/...)을 읽습니다.
        /// - gameMode  : ResourceManager를 통해 Metas/Chunks에서 바이트를 로드해서 JSON으로 파싱합니다.
        /// (현재는 UI를 .scene에서 함께 로드합니다. uiWorldManager는 사용하지 않습니다.)
        bool LoadAuto(World& world, const ResourceManager& resources, const std::filesystem::path& logicalPath, UIWorldManager* uiWorldManager = nullptr);

        /// .scene 파일에서 Scene 이름을 읽어옵니다.
        /// 파일이 없거나 이름이 없으면 빈 문자열을 반환합니다.
        std::string GetSceneName(const std::filesystem::path& path);
    }
}



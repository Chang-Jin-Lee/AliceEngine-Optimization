#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Runtime/ECS/Entity.h"

namespace Alice
{
    class World;
    class ResourceManager;

    /// 프리팹 로더/인스턴시에이터 및 저장 유틸입니다.
    /// - JSON 파일에서 Transform + Scripts[](+프로퍼티) 를 읽어오거나 저장합니다.
    /// - Unity 의 Prefab / Instantiate 개념을 간단하게 흉내내기 위한 용도입니다.
    namespace Prefab
    {
        struct DependencyInfo
        {
            std::vector<std::string> unityEffectPaths;
            std::vector<std::string> computeShaderNames;
        };

        /// 엔진 전역 기본 World 를 등록합니다. (InstantiateFromFileAuto에서 사용)
        void SetDefaultWorld(World* world);
        /// 엔진 전역 기본 ResourceManager 를 등록합니다. (InstantiateFromFileAuto에서 사용)
        void SetDefaultResources(ResourceManager* resources);

        /// 프리팹 파일을 읽어 새로운 엔티티를 생성합니다.
        /// \return 생성된 엔티티 ID (실패 시 InvalidEntityId)
        EntityId InstantiateFromFile(World& world, const std::filesystem::path& path);

        /// 논리 경로(Assets/..., Resource/..., Cooked/...)를 자동 로드하여 인스턴스화합니다.
        /// - editorMode: 원본 파일에서 로드
        /// - gameMode  : Cooked/Chunks에서 로드
        /// \return 생성된 엔티티 ID (실패 시 InvalidEntityId)
        EntityId InstantiateFromFileAuto(const std::filesystem::path& logicalPath);

        /// 논리 경로 프리팹 JSON을 미리 파싱/캐시합니다. (빌드 로딩 단계 워밍업용)
        bool PreloadJsonAuto(const std::filesystem::path& logicalPath);

        /// 프리팹에 선언된 UnityVfx/ComputeEffect 의존성을 수집합니다.
        bool CollectDependenciesAuto(const std::filesystem::path& logicalPath, DependencyInfo& outInfo);

        /// 현재 월드에 존재하는 엔티티를 프리팹 파일로 저장합니다.
        /// - Transform 과 Script 이름 한 개를 간단한 텍스트 포맷으로 기록합니다.
        /// - 같은 포맷을 InstantiateFromFile 이 다시 읽어서 엔티티를 생성할 수 있습니다.
        /// \return 저장 성공 여부
        bool SaveToFile(const World& world,
                        EntityId entity,
                        const std::filesystem::path& path);
    }
}




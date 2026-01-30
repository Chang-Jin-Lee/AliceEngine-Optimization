#pragma once

#include <filesystem>

#include "Core/Entity.h"

namespace Alice
{
    class World;

    /// 프리팹 로더/인스턴시에이터 및 저장 유틸입니다.
    /// - JSON 파일에서 Transform + Scripts[](+프로퍼티) 를 읽어오거나 저장합니다.
    /// - Unity 의 Prefab / Instantiate 개념을 간단하게 흉내내기 위한 용도입니다.
    namespace Prefab
    {
        /// 프리팹 파일을 읽어 새로운 엔티티를 생성합니다.
        /// \return 생성된 엔티티 ID (실패 시 InvalidEntityId)
        EntityId InstantiateFromFile(World& world, const std::filesystem::path& path);

        /// 현재 월드에 존재하는 엔티티를 프리팹 파일로 저장합니다.
        /// - Transform 과 Script 이름 한 개를 간단한 텍스트 포맷으로 기록합니다.
        /// - 같은 포맷을 InstantiateFromFile 이 다시 읽어서 엔티티를 생성할 수 있습니다.
        /// \return 저장 성공 여부
        bool SaveToFile(const World& world,
                        EntityId entity,
                        const std::filesystem::path& path);
    }
}




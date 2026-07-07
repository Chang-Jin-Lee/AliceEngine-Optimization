#pragma once

namespace Alice
{
    class World;

    /// 스크립트 DLL 로드/언로드의 단일 관문.
    /// Unity 도메인 리로드 방식: 스냅샷(JSON) → 인스턴스 전부 파괴 →
    /// 콜백 클리어 → 언로드 → (복사본) 로드 → 복원.
    /// 다른 코드는 ScriptHotReload_*를 직접 호출하지 말 것.
    namespace ScriptDomain
    {
        /// 엔진 시작 시 최초 로드 (복원할 상태 없음)
        bool LoadInitial();

        /// 새 DLL로 교체. 스크립트 상태는 JSON으로 보존 후 복원된다.
        bool Reload(World& world);

        /// 게임 빌드 직전 등 DLL을 완전히 내려야 할 때. 복원하지 않는다.
        bool Unload(World& world);

        bool IsLoaded();
    }
}

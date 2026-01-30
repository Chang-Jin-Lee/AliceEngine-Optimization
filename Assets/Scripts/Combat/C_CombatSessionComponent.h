#pragma once
/*
* 전투 루프 총괄. 플레이어/보스 Fighter·FSM·이벤트버스·리졸버를 갖고, 매 프레임 Intent → Sensors → FSM → Command → 적용 순서로 돌림.
* 씬의 매니저용 엔티티 (빈 오브젝트 등). m_playerGuid, m_bossGuid로 플레이어/보스 엔티티를 찾음.
*/

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "Core/Entity.h"
#include <memory>

namespace Alice
{
    class C_CombatSessionComponent : public IScript
    {
        ALICE_BODY(C_CombatSessionComponent);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void PostCombatUpdate(float deltaTime) override;
        void OnEnable() override;
        void OnDisable() override;

        ALICE_PROPERTY(uint64_t, m_playerGuid, 0);
        ALICE_PROPERTY(uint64_t, m_bossGuid, 0);
        ALICE_PROPERTY(bool, m_enableLogs, false);

        void ForceReset();
        ALICE_FUNC(ForceReset);

    private:
        EntityId ResolveEntity(uint64_t guid) const;

        struct SessionState;
        std::unique_ptr<SessionState> m_state;
    };
}

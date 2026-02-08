#pragma once

#include <string>
#include <cstdint>
#include <vector>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "AudioSoundState.h"
#include "../Combat/C_BossBrainComponent.h"

namespace Alice
{
    class C_CombatSessionComponent;
    class AudioEventBusScript;

    /// 전투 FSM/Resolver에서 호출하는 델리게이트를 구독해 사운드 요청으로 연결.
    /// 플레이어/보스 엔티티에 각각 붙이고, isPlayer로 역할을 구분한다.
    class SoundBridgeScript : public IScript
    {
        ALICE_BODY(SoundBridgeScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        /// Session 컴포넌트가 붙은 엔티티 이름 (예: SceneManager)
        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager");
        /// AudioEventBusScript가 붙은 엔티티 이름 (예: AudioBus)
        ALICE_PROPERTY(std::string, busEntityName, "AudioBus");
        ALICE_PROPERTY(float, combo2ExtraDelaySec, 1.0f);
        ALICE_PROPERTY(bool, combo2ExtraEnabled, true);

    private:
        //C_CombatSessionComponent* FindSession();
        //AudioEventBusScript* FindBus();
        void OnCombatStateEntered(EntityId entityId, std::uint8_t prevState, std::uint8_t curState, const void* flagsPtr);
        void OnCombatResolve(EntityId victimId, EntityId attackerId, std::uint8_t resolveResult, float damage, const DirectX::XMFLOAT3& hitPos, const DirectX::XMFLOAT3& hitNormal);
        void OnPhase2Entered();
        
        // 보스 패턴의 실제 애니메이션 재생 시간 계산 (속도 반영)
        float GetBossPatternActualDuration(EntityId bossId, C_BossBrainComponent::PatternType patternType);

        std::uint8_t m_prevActionState = 0xFF;
        std::uint8_t m_prevBossState = 0xFF;
        int m_prevComboIndex = -1;
        bool m_combo2ExtraPending = false;
        float m_combo2ExtraTimer = 0.0f;
        
        // 보스 패턴 추적 (Charge → SoulSwordAttack 전환 감지용)
        int m_prevBossPattern = -1;  // PatternType을 int로 저장 (None = 0)
        
        // 그로기 진입 후 Hit SFX 억제 타이머
        float m_suppressBossHitSfxTimer = 0.0f;
    };
}

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <DirectXMath.h>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/GameObject.h"

// [수정] ComputeEffectComponent 헤더 포함 (필요 시) 혹은 전방 선언
// 여기서는 cpp에서 포함하고, 구조체는 GameObject를 들고 있으므로 그대로 둡니다.

namespace Alice
{
    // 캐릭터의 동작 상태 정의
    enum class CharState
    {
        Standing,    // 서기 (이동 및 공격 가능)
        Crouching,   // 앉는 중
        Crouched,    // 앉음 (이동 불가, 공격 불가)
        StandingUp,  // 일어서는 중
        Attacking    // 공격 중 (이동 불가, 다른 동작 불가)
    };

    // [수정] 활성화된 ComputeEffect 정보를 관리하기 위한 구조체
    struct TimedComputeEffect
    {
        GameObject go;
        float remainingTime;
    };

    class CharacterAnimatorComponent : public IScript
    {
        ALICE_BODY(CharacterAnimatorComponent);

    public:
        void Update(float DeltaTime) override;
        void Start() override;

        // 노티파이용 함수
        void OnAttackHit();
        void OnCrouchHalfway();

        // [수정] ComputeEffect 생성 헬퍼 함수
        void SpawnComputeEffect(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& color, float size, float duration);

        // --- Movement settings ---
        ALICE_PROPERTY(float, m_moveSpeed, 10.0f);
        ALICE_PROPERTY(float, m_runMultiplier, 1.6f);
        ALICE_PROPERTY(float, m_jumpSpeed, 6.5f);
        ALICE_PROPERTY(float, m_gravity, 18.0f);
        ALICE_PROPERTY(float, m_blendSpeed, 8.0f);

        // --- Base layer clips ---
        ALICE_PROPERTY(std::string, m_idleClip, "Idle");
        ALICE_PROPERTY(std::string, m_walkClip, "Walk");
        ALICE_PROPERTY(std::string, m_runClip, "Run");

        // --- Crouch clips ---
        ALICE_PROPERTY(std::string, m_crouchClip, "CrouchDown");
        ALICE_PROPERTY(float, m_crouchDuration, 1.0f);

        // --- Attack clips ---
        ALICE_PROPERTY(std::string, m_attackClip, "Attack01");
        ALICE_PROPERTY(float, m_attackPlaySpeed, 1.0f);
        ALICE_PROPERTY(float, m_attackDuration, 1.5f);
        ALICE_PROPERTY(float, m_attackHitTime, 0.7f);
        ALICE_PROPERTY(float, m_transitionDuration, 0.2f);

        // --- [수정] Attack ComputeEffect Settings ---
        // 공격 시 생성되는 파티클 설정 (3초 뒤 사라짐)
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_attackComputeColor, DirectX::XMFLOAT3(1.0f, 0.2f, 0.2f)); // 붉은색
        ALICE_PROPERTY(float, m_attackComputeSize, 6.0f); // 픽셀 단위 크기
        ALICE_PROPERTY(float, m_attackComputeDuration, 3.0f); // 3초 뒤 사라짐
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_attackComputeOffset, DirectX::XMFLOAT3(0.0f, 1.0f, 1.0f)); // 캐릭터 앞쪽

        // --- [수정] Walk ComputeEffect Settings ---
        // 걷을 때 생성되는 파티클 설정 (1초 뒤 사라짐)
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_walkComputeColor, DirectX::XMFLOAT3(0.8f, 0.8f, 0.8f)); // 흰색 먼지
        ALICE_PROPERTY(float, m_walkComputeSize, 3.0f); // 픽셀 단위 크기
        ALICE_PROPERTY(float, m_walkComputeDuration, 1.0f); // 1초 뒤 사라짐
        ALICE_PROPERTY(float, m_walkSpawnInterval, 0.3f);    // 0.3초마다 생성
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_walkComputeOffset, DirectX::XMFLOAT3(0.0f, 0.1f, 0.0f)); // 발 밑

        // --- Upper layer clips ---
        ALICE_PROPERTY(bool, m_enableUpperLayer, false);
        ALICE_PROPERTY(std::string, m_upperClip, "Aim");

        // --- Additive clips ---
        ALICE_PROPERTY(bool, m_enableAdditive, false);
        ALICE_PROPERTY(std::string, m_additiveClip, "Recoil");
        ALICE_PROPERTY(std::string, m_additiveRefClip, "Idle");
        ALICE_PROPERTY(float, m_additiveDuration, 0.25f);

        // --- Socket setup ---
        ALICE_PROPERTY(std::string, m_socketName, "WeaponPoint");
        ALICE_PROPERTY(std::string, m_socketParentBone, "Hand_R");
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_socketPos, DirectX::XMFLOAT3(0.1f, 0.05f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_socketRotDeg, DirectX::XMFLOAT3(0.0f, 90.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_socketScale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        // 무기 부착 관련
        ALICE_PROPERTY(std::string, m_weaponObjName, "Weapon");

        // --- IK settings ---
        ALICE_PROPERTY(bool, m_enableIK, false);
        ALICE_PROPERTY(std::string, m_ikTipBone, "Hand_L");
        ALICE_PROPERTY(int, m_ikChainLength, 3);
        ALICE_PROPERTY(float, m_ikWeight, 1.0f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_ikTargetLocal, DirectX::XMFLOAT3(0.0f, 1.2f, 0.2f));

        // --- Foot IK settings ---
        ALICE_PROPERTY(bool, m_enableFootIK, true);
        ALICE_PROPERTY(std::string, m_leftFootBone, "Ball_L");
        ALICE_PROPERTY(float, m_ikLiftSpeed, 5.0f);
        ALICE_PROPERTY(float, m_maxLiftHeight, 0.5f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_leftFootBasePos, DirectX::XMFLOAT3(-0.2f, 0.0f, 0.1f));

        // --- Aim ---
        ALICE_PROPERTY(bool, m_enableAim, false);
        ALICE_PROPERTY(float, m_aimYawDeg, 0.0f);
        ALICE_PROPERTY(float, m_aimWeight, 1.0f);

    private:
        float m_velY = 0.0f;
        float m_moveBlend = 0.0f;
        float m_additiveTimer = -1.0f;
        bool m_socketInitialized = false;
        std::string m_lastMoveClip;

        CharState m_state = CharState::Standing;
        float m_currentCrouchTime = 0.0f;

        bool m_notifyRegistered = false;
        std::uint64_t m_attackNotifyTag = 0;
        std::uint64_t m_crouchNotifyTag = 0;

        float m_currentAttackTime = 0.0f;
        bool m_isAttackReversing = false;

        float m_blendTimer = 0.0f;
        bool m_isBlendingOut = false;

        // [수정] ComputeEffect 관리용 변수
        float m_walkTimer = 0.0f;
        std::vector<TimedComputeEffect> m_activeComputeEffects;

        float m_currentLeftFootHeight = 0.0f;
        float m_animSpeed = 1.0f;
        bool m_isStretchedMode = false;

        bool m_isWeaponAttached = false;
        GameObject m_weaponGo;
        TransformComponent* Tr = nullptr;
        bool m_isSetupFinished = false;
    };
}

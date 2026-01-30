//#pragma once
//
//#include "Core/IScript.h"
//#include "Core/ScriptReflection.h"
//
//namespace Alice
//{
//    /// 유니티 느낌의 간단한 카메라 팔로우 스크립트입니다.
//    /// - FixedUpdate에서 "첫번째 SkinnedMesh 엔티티"를 따라갑니다.
//    /// - 목표가 없으면 아무 것도 하지 않습니다.
//    class CameraFollow : public IScript
//    {
//    public:
//        const char* GetName() const override { return "CameraFollow"; }
//
//        void LateUpdate(float lateDeltaTime) override;
//
//    private:
//        // 간단 오프셋 (유니티의 third-person 카메라 느낌)
//        ALICE_SERIALIZE_FIELD(float, m_distance, 0.0f);         // 타겟과의 거리
//        ALICE_SERIALIZE_FIELD(float, m_sensitivity, 28.0f);     // 마우스 회전 감도
//        ALICE_SERIALIZE_FIELD(float, m_heightOffset, -13.0f);   // 타겟의 높이 보정 (머리 위 등)
//    };
//}

//
//#pragma once
//
//#include "Core/IScript.h"
//#include "Core/ScriptReflection.h"
//
//namespace Alice
//{
//    /// 유니티 느낌의 간단한 카메라 팔로우 스크립트입니다.
//    /// - FixedUpdate에서 "첫번째 SkinnedMesh 엔티티"를 따라갑니다.
//    /// - 목표가 없으면 아무 것도 하지 않습니다.
//    class CameraFollow : public IScript
//    {
//    public:
//        const char* GetName() const override { return "CameraFollow"; }
//
//        void MoveDirectly();
//        void MoveLerp(const float& lateDeltaTime);
//
//        void LateUpdate(float lateDeltaTime) override;
//
//    private:
//        // 간단 오프셋 (유니티의 third-person 카메라 느낌)
//        ALICE_SERIALIZE_FIELD(float, m_offsetX, 0.0f);
//        ALICE_SERIALIZE_FIELD(float, m_offsetY, 28.0f);
//        ALICE_SERIALIZE_FIELD(float, m_offsetZ, -13.0f);
//        ALICE_SERIALIZE_FIELD(float, m_smoothSpeed, 0.0f);
//    };
//}
//
//


#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    class CameraFollow : public IScript
    {
        ALICE_BODY(CameraFollow);

    public:
        void LateUpdate(float lateDeltaTime) override;

        // 고정 오프셋 이동
        void MoveDirectly();
        ALICE_FUNC(MoveDirectly);

        void MoveLerp(const float& lateDeltaTime);
        ALICE_FUNC(MoveLerp);

        // 마우스 드래그로 캐릭터 주변 회전
        void MoveOrbit();
        ALICE_FUNC(MoveOrbit);

        // MoveDirectly 변수
        ALICE_PROPERTY(float, m_offsetX, 0.0f);
        ALICE_PROPERTY(float, m_offsetY, 28.0f);
        ALICE_PROPERTY(float, m_offsetZ, -13.0f);
        ALICE_PROPERTY(float, m_smoothSpeed, 0.0f);

        // MoveOrbit 변수
        ALICE_PROPERTY(float, m_distance, 35.0f);      // 캐릭터와의 거리 (Arm Length)
        ALICE_PROPERTY(float, m_heightOffset, 1.5f);  // 회전 중심점 높이 (머리/가슴)
        ALICE_PROPERTY(float, m_sensitivity, 0.2f);   // 마우스 감도
        ALICE_PROPERTY(float, m_currentYaw, 0.0f);    // 현재 좌우 각도
        ALICE_PROPERTY(float, m_currentPitch, 20.0f); // 현재 상하 각도
        ALICE_PROPERTY(float, m_minDistance, 10.0f);   // 최소 거리 (너무 가까워짐 방지)
        ALICE_PROPERTY(float, m_maxDistance, 55.0f);  // 최대 거리
        ALICE_PROPERTY(float, m_zoomSpeed, 0.01f);     // 휠 줌 속도
    };
}
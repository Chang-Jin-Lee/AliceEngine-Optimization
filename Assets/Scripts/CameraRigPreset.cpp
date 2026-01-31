#include "CameraRigPreset.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"

#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraInputComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"

#include <DirectXMath.h>

namespace Alice
{
    REGISTER_SCRIPT(CameraRigPreset);

    void CameraRigPreset::Awake()
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        // 메인 카메라 보장
        auto* cam = go.GetComponent<CameraComponent>();
        if (!cam) cam = &go.AddComponent<CameraComponent>();
        cam->primary = true;

        auto* input = go.GetComponent<CameraInputComponent>();
        if (!input) input = &go.AddComponent<CameraInputComponent>();
        auto* blend = go.GetComponent<CameraBlendComponent>();
        if (!blend) blend = &go.AddComponent<CameraBlendComponent>();
        auto* shake = go.GetComponent<CameraShakeComponent>();
        if (!shake) shake = &go.AddComponent<CameraShakeComponent>();
        auto* spring = go.GetComponent<CameraSpringArmComponent>();
        if (!spring) spring = &go.AddComponent<CameraSpringArmComponent>();
        auto* follow = go.GetComponent<CameraFollowComponent>();
        if (!follow) follow = &go.AddComponent<CameraFollowComponent>();
        auto* lookAt = go.GetComponent<CameraLookAtComponent>();
        if (!lookAt) lookAt = &go.AddComponent<CameraLookAtComponent>();
        // LookAt은 기본 OFF (L키로만 켜지게)
        if (lookAt) lookAt->enabled = false;

        // 입력 프리셋
        input->cameraListCsv = Get_m_cameraListCsv();
        input->blendTimeKey3 = Get_m_blendTimeKey3();
        input->blendTimeKey4 = Get_m_blendTimeKey4();
        input->blendTimeKey5 = Get_m_blendTimeKey5();

        input->shakeAmplitudeKey4 = Get_m_shakeAmplitudeKey4();
        input->shakeFrequencyKey4 = Get_m_shakeFrequencyKey4();
        input->shakeDurationKey4 = Get_m_shakeDurationKey4();
        input->shakeDecayKey4 = Get_m_shakeDecayKey4();

        input->slowTriggerTKey5 = Get_m_slowTriggerTKey5();
        input->slowDurationKey5 = Get_m_slowDurationKey5();
        input->slowTimeScaleKey5 = Get_m_slowTimeScaleKey5();

        input->lookAtTargetName = Get_m_lookAtTargetName();

        // 스프링 암 기본값
        spring->distance = Get_m_springArmDistance();
        spring->desiredDistance = spring->distance;
        spring->minDistance = Get_m_springArmMinDistance();
        spring->maxDistance = Get_m_springArmMaxDistance();
        spring->zoomSpeed = Get_m_springArmZoomSpeed();

        // TPS 입력 기본값
        follow->enableInput = true;
        follow->targetName = Get_m_followTargetName();
        if (auto* tr = go.GetComponent<TransformComponent>())
        {
            follow->yawDeg = DirectX::XMConvertToDegrees(tr->rotation.y);
            follow->pitchDeg = DirectX::XMConvertToDegrees(tr->rotation.x);
        }
    }

    void CameraRigPreset::Update(float /*deltaTime*/)
    {
        if (!Get_m_applyEveryFrame())
            return;

        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto* input = go.GetComponent<CameraInputComponent>();
        if (!input) input = &go.AddComponent<CameraInputComponent>();
        auto* spring = go.GetComponent<CameraSpringArmComponent>();
        if (!spring) spring = &go.AddComponent<CameraSpringArmComponent>();
        auto* follow = go.GetComponent<CameraFollowComponent>();
        if (!follow) follow = &go.AddComponent<CameraFollowComponent>();

        input->cameraListCsv = Get_m_cameraListCsv();
        input->blendTimeKey3 = Get_m_blendTimeKey3();
        input->blendTimeKey4 = Get_m_blendTimeKey4();
        input->blendTimeKey5 = Get_m_blendTimeKey5();

        input->shakeAmplitudeKey4 = Get_m_shakeAmplitudeKey4();
        input->shakeFrequencyKey4 = Get_m_shakeFrequencyKey4();
        input->shakeDurationKey4 = Get_m_shakeDurationKey4();
        input->shakeDecayKey4 = Get_m_shakeDecayKey4();

        input->slowTriggerTKey5 = Get_m_slowTriggerTKey5();
        input->slowDurationKey5 = Get_m_slowDurationKey5();
        input->slowTimeScaleKey5 = Get_m_slowTimeScaleKey5();

        input->lookAtTargetName = Get_m_lookAtTargetName();

        follow->targetName = Get_m_followTargetName();

        spring->distance = Get_m_springArmDistance();
        spring->desiredDistance = spring->distance;
        spring->minDistance = Get_m_springArmMinDistance();
        spring->maxDistance = Get_m_springArmMaxDistance();
        spring->zoomSpeed = Get_m_springArmZoomSpeed();
    }
}

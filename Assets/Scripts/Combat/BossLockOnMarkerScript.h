#pragma once

#include <string>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class C_CombatSessionComponent;
    struct UIImageComponent;
    struct UIWidgetComponent;

    class BossLockOnMarkerScript : public IScript
    {
        ALICE_BODY(BossLockOnMarkerScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager");
        ALICE_PROPERTY(std::string, bossEntityName, "Boss");
        ALICE_PROPERTY(std::string, cameraEntityName, "MainCamera");
        ALICE_PROPERTY(std::string, markerEntityName, "__BossLockOnMarkerWorld");

        ALICE_PROPERTY(float, markerYOffset, 1.5f);
        ALICE_PROPERTY(float, markerSizeX, 220.0f);
        ALICE_PROPERTY(float, markerSizeY, 220.0f);
        ALICE_PROPERTY(float, markerWorldScale, 0.01f);
        ALICE_PROPERTY(float, markerTowardCameraOffset, 0.35f);
        ALICE_PROPERTY(bool, enableDistanceScale, true);
        ALICE_PROPERTY(float, distanceScaleNearDistance, 2.0f);
        ALICE_PROPERTY(float, distanceScaleFarDistance, 8.0f);
        ALICE_PROPERTY(float, distanceScaleNearMultiplier, 0.45f);
        ALICE_PROPERTY(float, distanceScaleFarMultiplier, 1.0f);

        ALICE_PROPERTY(std::string, lockOnTexturePath, "Resource/Test/4_Resources/UI/LockOn/LockON.png");
        ALICE_PROPERTY(std::string, groggyTexturePath, "Resource/Test/4_Resources/UI/LockOn/Groggy.png");

    private:
        void ResolveSession();
        void ResolveBoss();
        void ResolveCamera();
        void EnsureMarkerEntity();
        void EnsureMarkerAttachment();
        void ApplyMarkerWorldScale(float distanceToCamera);
        void SetMarkerVisible(bool visible);
        void SetMarkerTexture(const std::string& path);

        C_CombatSessionComponent* m_session = nullptr;
        EntityId m_bossId = InvalidEntityId;
        EntityId m_cameraId = InvalidEntityId;
        EntityId m_markerId = InvalidEntityId;
        UIImageComponent* m_markerImage = nullptr;
        UIWidgetComponent* m_markerWidget = nullptr;
    };
}

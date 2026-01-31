#pragma once

#include "Runtime/Resources/Scene.h"

namespace Alice
{
    /// 가장 기본이 되는 테스트용 씬입니다.
    /// - 큐브 엔티티 하나를 생성하고
    /// - 시간에 따라 Y축으로 회전시킵니다.
    class SampleScene : public IScene
    {
    public:
        SampleScene()  = default;
        ~SampleScene() override = default;

        const char* GetName() const override { return "SampleScene"; }

        void OnEnter(World& world, ResourceManager& resources) override;
        void OnExit(World& world, ResourceManager& resources) override;
        void Update(World& world, ResourceManager& resources, float deltaTime) override;

        EntityId GetPrimaryRenderableEntity() const override { return m_cubeEntity; }

    private:
        EntityId m_cubeEntity { InvalidEntityId };
        float    m_rotationSpeed = 1.0f; // rad/sec
    };
}




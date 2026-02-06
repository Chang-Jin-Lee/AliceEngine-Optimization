#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"

namespace Alice
{
    // UnityVfxComponent를 손쉽게 스폰하기 위한 스크립트
    class UnityVfxSpawner : public IScript
    {
        ALICE_BODY(UnityVfxSpawner);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // 에디터/스크립트에서 호출 가능한 스폰 함수
        void Spawn();
        ALICE_FUNC(Spawn);

    private:
        EntityId SpawnInternal(const std::string& path);

        // 기본 설정
        ALICE_PROPERTY(std::string, m_effectPath, std::string("Resource/VFX/UnityExport/Combo_slash_fx_01/effect.json"));
        ALICE_PROPERTY(bool, m_playOnStart, true);
        ALICE_PROPERTY(bool, m_followOwner, true);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_localOffset, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_localRotationDeg, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_localScale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));
        ALICE_PROPERTY(float, m_autoDestroySec, 3.0f);

        // UnityVfxComponent override
        ALICE_PROPERTY(bool, m_useMeshRenderer, true);
        ALICE_PROPERTY(bool, m_useComputeEffect, false);
        ALICE_PROPERTY(float, m_timeScale, 1.0f);
        ALICE_PROPERTY(float, m_lifetimeScale, 1.0f);
        ALICE_PROPERTY(bool, m_overrideLoop, false);
        ALICE_PROPERTY(bool, m_loop, true);
        ALICE_PROPERTY(float, m_sizeScale, 10.0f);
        ALICE_PROPERTY(float, m_speedScale, 1.0f);
        ALICE_PROPERTY(float, m_intensityScale, 1.0f);
        ALICE_PROPERTY(float, m_spawnRateScale, 1.0f);
        ALICE_PROPERTY(bool, m_enableTrails, true);
        ALICE_PROPERTY(float, m_trailWidthScale, 1.0f);
        ALICE_PROPERTY(float, m_trailLifeScale, 1.0f);
    };
}

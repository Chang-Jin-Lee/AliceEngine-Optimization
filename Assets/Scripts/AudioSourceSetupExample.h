#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// AudioSourceComponent를 설정하고 디버그 기능을 테스트하는 예시
    class AudioSourceSetupExample : public IScript
    {
        ALICE_BODY(AudioSourceSetupExample);

    public:
        void Start() override;
        void Update(float dt) override;

    public:
        ALICE_PROPERTY(std::string, soundPath,    std::string("Assets/Sounds/Effect.wav"));
        ALICE_PROPERTY(bool,        is3D,         true);
        ALICE_PROPERTY(float,       volume,       1.0f);
        ALICE_PROPERTY(float,       minDistance,  1.0f);
        ALICE_PROPERTY(float,       maxDistance, 15.0f);
        ALICE_PROPERTY(bool,        debugDraw,    true); // 기본값 true로 설정하여 즉시 확인
    };
}

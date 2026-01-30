#pragma once

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include <string>
#include <vector>

namespace Alice
{
    /// 오디오 데모 스크립트
    /// 1번키: BGM 재생 (컴포넌트 방식)
    /// 2~6번키: VFX 효과음 재생 (직접 호출 방식)
    /// 7번키: BGM 정지
    /// 8번키: 모든 VFX 정지
    /// 9번키: 모든 소리 정지
    class AudioDemoScript : public IScript
    {
        ALICE_BODY(AudioDemoScript);

    public:
        void Start() override;
        void Update(float dt) override;

    public:
        // 에디터에서 설정할 경로들
        ALICE_PROPERTY(std::string, bgmPath, std::string("Resource/Sound/MyBGM.mp3"));
        ALICE_PROPERTY(std::string, vfxPath1, std::string("Resource/Sound/Attack.wav"));
        ALICE_PROPERTY(std::string, vfxPath2, std::string("Resource/Sound/Jump.wav"));
        ALICE_PROPERTY(std::string, vfxPath3, std::string("Resource/Sound/Hit.wav"));
        ALICE_PROPERTY(std::string, vfxPath4, std::string("Resource/Sound/Coin.wav"));
        ALICE_PROPERTY(std::string, vfxPath5, std::string("Resource/Sound/Explosion.wav"));
    };
}
